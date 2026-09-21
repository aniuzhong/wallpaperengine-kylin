#include "systemd_unit.h"

#include <systemd/sd-bus.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <type_traits>
#include <utility>

namespace systemd {

namespace {

constexpr const char* kService = "org.freedesktop.systemd1";
constexpr const char* kManagerPath = "/org/freedesktop/systemd1";
constexpr const char* kManagerIface = "org.freedesktop.systemd1.Manager";
constexpr const char* kUnitIface = "org.freedesktop.systemd1.Unit";

// Map a failed sd-bus call onto the typed error. D-Bus error names are
// preserved for diagnostics; kind is what callers branch on.
void TakeError(int rc, const sd_bus_error& err, wallpaper_engine::Error* error) {
    if (error == nullptr)
        return;
    if (err.name != nullptr) {
        error->dbusName = err.name;
        error->message = err.message != nullptr ? err.message : "";
        error->kind = std::strstr(err.name, "NoSuchUnit") != nullptr ? wallpaper_engine::Error::NoSuchUnit
                                                                     : wallpaper_engine::Error::Unknown;
    } else {
        error->kind = wallpaper_engine::Error::Unknown;
        error->message = std::strerror(-rc);
    }
}

// Fire one manager method (varargs-encoded arguments) on an open connection.
bool CallManager(sd_bus* bus, const char* method, const char* types, wallpaper_engine::Error* error, ...) {
    va_list ap;
    va_start(ap, error);
    sd_bus_message* m = nullptr;
    int rc = sd_bus_message_new_method_call(bus, &m, kService, kManagerPath, kManagerIface, method);
    if (rc >= 0)
        rc = sd_bus_message_appendv(m, types, ap);
    va_end(ap);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (rc >= 0)
        rc = sd_bus_call(bus, m, 0, &err, &reply);

    const bool ok = rc >= 0;
    if (!ok)
        TakeError(rc, err, error);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return ok;
}

// Shared body of the lifecycle methods: one "ss" call with the canonical
// unit id and replace mode.
Result<void> UnitMethod(sd_bus* bus, const char* method, const std::string& unit) {
    wallpaper_engine::Error error;
    if (CallManager(bus, method, "ss", &error, CanonicalUnitName(unit).c_str(), "replace"))
        return {};
    return tl::unexpected(std::move(error));
}

std::string UnitObjectPathFromId(const std::string& unitId) {
    // systemd escapes every non [A-Za-z0-9] byte of the unit id as _XX
    std::string escaped;
    char hex[5];
    for (const char c : unitId) {
        const unsigned char u = static_cast<unsigned char> (c);
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9'))
            escaped += c;
        else {
            std::snprintf(hex, sizeof hex, "_%02x", u);
            escaped += hex;
        }
    }
    return "/org/freedesktop/systemd1/unit/" + escaped;
}

} // namespace

UnitState UnitStateFromName(const std::string& name) {
    if (name == "active")
        return UnitState::Active;
    if (name == "reloading")
        return UnitState::Reloading;
    if (name == "inactive")
        return UnitState::Inactive;
    if (name == "failed")
        return UnitState::Failed;
    if (name == "activating")
        return UnitState::Activating;
    if (name == "deactivating")
        return UnitState::Deactivating;
    return UnitState::Unknown;
}

const char* UnitStateName(UnitState state) {
    switch (state) {
        case UnitState::Active: return "active";
        case UnitState::Reloading: return "reloading";
        case UnitState::Inactive: return "inactive";
        case UnitState::Failed: return "failed";
        case UnitState::Activating: return "activating";
        case UnitState::Deactivating: return "deactivating";
        case UnitState::Unknown: break;
    }
    return "unknown";
}

Connection::Connection(Connection&& other) noexcept : bus_(other.bus_) {
    other.bus_ = nullptr;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (bus_ != nullptr)
            sd_bus_flush_close_unref(bus_);
        bus_ = other.bus_;
        other.bus_ = nullptr;
    }
    return *this;
}

Connection::~Connection() {
    if (bus_ != nullptr)
        sd_bus_flush_close_unref(bus_);
}

Result<Connection> Connection::UserBus() {
    Connection connection;
    if (sd_bus_open_user(&connection.bus_) < 0 || connection.bus_ == nullptr) {
        wallpaper_engine::Error error;
        error.kind = wallpaper_engine::Error::BusUnreachable;
        error.message = "cannot connect to the user bus";
        return tl::unexpected(std::move(error));
    }
    return connection;
}

Result<void> Start(Connection& connection, const std::string& unit) {
    return UnitMethod(connection.handle(), "StartUnit", unit);
}

Result<void> Stop(Connection& connection, const std::string& unit) {
    return UnitMethod(connection.handle(), "StopUnit", unit);
}

Result<void> Restart(Connection& connection, const std::string& unit) {
    return UnitMethod(connection.handle(), "RestartUnit", unit);
}

Result<void> ResetFailed(Connection& connection, const std::string& unit) {
    wallpaper_engine::Error error;
    CallManager(connection.handle(), "ResetFailedUnit", "s", &error, CanonicalUnitName(unit).c_str());
    if (Tolerated(error))
        error = {};
    if (error.kind == wallpaper_engine::Error::NoError)
        return {};
    return tl::unexpected(std::move(error));
}

Result<void> DaemonReload(Connection& connection) {
    wallpaper_engine::Error error;
    if (CallManager(connection.handle(), "Reload", "", &error))
        return {};
    return tl::unexpected(std::move(error));
}

// Marshal one caller-chosen scalar into an in-message variant.
int AppendPropertyValue(sd_bus_message* m, const UnitPropertyValue& value) {
    return std::visit(
        [m](const auto& v) -> int {
            using T = std::decay_t<decltype (v)>;
            if constexpr(std::is_same_v<T, std::string>)
                return sd_bus_message_append(m, "v", "s", v.c_str());
            else if constexpr(std::is_same_v<T, bool>)
                return sd_bus_message_append(m, "v", "b", int(v));
            else if constexpr(std::is_same_v<T, int64_t>)
                return sd_bus_message_append(m, "v", "x", v);
            else
                return sd_bus_message_append(m, "v", "d", v);
        },
        value);
}

// Append the ExecStart=(sasb) property: one entry carrying the full argv,
// failure tolerance off.
int AppendExecStart(sd_bus_message* m, const ExecCommand& command) {
    int rc = sd_bus_message_open_container(m, SD_BUS_TYPE_STRUCT, "sv");
    if (rc >= 0) rc = sd_bus_message_append(m, "s", "ExecStart");
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "a(sasb)");
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "(sasb)");
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_STRUCT, "sasb");
    if (rc >= 0) rc = sd_bus_message_append(m, "s", command.program.c_str());
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "s");
    for (const std::string& arg : command.args) {
        if (rc < 0)
            break;
        rc = sd_bus_message_append(m, "s", arg.c_str());
    }
    if (rc >= 0) rc = sd_bus_message_close_container(m); // argv
    if (rc >= 0) rc = sd_bus_message_append(m, "b", 0);  // ignore-failure flag
    if (rc >= 0) rc = sd_bus_message_close_container(m); // (sasb)
    if (rc >= 0) rc = sd_bus_message_close_container(m); // a(sasb)
    if (rc >= 0) rc = sd_bus_message_close_container(m); // variant v
    if (rc >= 0) rc = sd_bus_message_close_container(m); // struct (sv)
    return rc;
}

Result<void> StartTransient(Connection& connection, const TransientSpec& spec) {
    if (spec.argv.empty()) {
        wallpaper_engine::Error error;
        error.kind = wallpaper_engine::Error::InvalidInput;
        error.message = "empty argv";
        return tl::unexpected(std::move(error));
    }

    // environment via /usr/bin/env prefix: systemd's transient Environment
    // property is not reliably applied to the exec'd process on all versions
    std::vector<std::string> finalArgs { "/usr/bin/env" };
    for (const auto& [key, value] : spec.environment)
        finalArgs.push_back(key + "=" + value);
    finalArgs.insert(finalArgs.end(), spec.argv.begin(), spec.argv.end());

    const ExecCommand command = ToExecCommand(finalArgs);
    sd_bus* bus = connection.handle();

    // a stale failed unit with the same name blocks re-creation ("already
    // exists") — clear it first; a not-loaded unit is a harmless no-op
    sd_bus_call_method(bus, kService, kManagerPath, kManagerIface, "ResetFailedUnit", nullptr, nullptr, "s",
                        CanonicalUnitName(spec.unit).c_str());

    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(bus, &m, kService, kManagerPath, kManagerIface, "StartTransientUnit") < 0)
        m = nullptr;

    const std::string unitId = CanonicalUnitName(spec.unit);
    int rc = -1;
    if (m != nullptr) rc = sd_bus_message_append(m, "ss", unitId.c_str(), "replace");
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "(sv)");
    if (rc >= 0) rc = sd_bus_message_append(m, "(sv)", "Description", "s", "wallpaper transient unit");
    if (rc >= 0) rc = sd_bus_message_append(m, "(sv)", "Type", "s", "simple");
    if (rc >= 0) rc = sd_bus_message_append(m, "(sv)", "Restart", "s", "on-failure");
    for (const auto& [key, value] : spec.extraProperties) {
        if (rc < 0)
            break;
        rc = sd_bus_message_open_container(m, SD_BUS_TYPE_STRUCT, "sv");
        if (rc >= 0) rc = sd_bus_message_append(m, "s", key.c_str());
        if (rc >= 0) rc = AppendPropertyValue(m, value);
        if (rc >= 0) rc = sd_bus_message_close_container(m);
    }
    if (rc >= 0) rc = AppendExecStart(m, command);
    if (rc >= 0) rc = sd_bus_message_close_container(m); // properties a(sv)
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "(sa(sv))");
    if (rc >= 0) rc = sd_bus_message_close_container(m); // aux: empty — systemd 245 expects a(sa(sv)),
                                                          // NOT the upstream-documented a(sba(sv))

    if (rc >= 0) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        rc = sd_bus_call(bus, m, 0, &err, &reply);
        sd_bus_message_unref(reply);
        if (rc < 0) {
            wallpaper_engine::Error error;
            TakeError(rc, err, &error);
            sd_bus_error_free(&err);
            sd_bus_message_unref(m);
            return tl::unexpected(std::move(error));
        }
        sd_bus_error_free(&err);
    } else {
        wallpaper_engine::Error error;
        error.kind = wallpaper_engine::Error::Unknown;
        error.message = std::strerror(-rc);
        sd_bus_message_unref(m);
        return tl::unexpected(std::move(error));
    }
    sd_bus_message_unref(m);
    return {};
}

Result<std::optional<UnitState>> ActiveState(Connection& connection, const std::string& unit) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char* state = nullptr;
    const int rc = sd_bus_get_property_string(connection.handle(), kService,
                                              UnitObjectPathFromId(CanonicalUnitName(unit)).c_str(), kUnitIface,
                                              "ActiveState", &err, &state);

    if (rc < 0) {
        // a known unit that is not loaded has no ActiveState interface —
        // that is the distinct "not loaded" state, reported as nullopt
        if (err.name != nullptr &&
            (std::strstr(err.name, "UnknownInterface") != nullptr || std::strstr(err.name, "NoSuchUnit") != nullptr)) {
            sd_bus_error_free(&err);
            return std::nullopt;
        }
        wallpaper_engine::Error error;
        TakeError(rc, err, &error);
        sd_bus_error_free(&err);
        return tl::unexpected(std::move(error));
    }

    const UnitState parsed = UnitStateFromName(state != nullptr ? state : "");
    free(state);
    sd_bus_error_free(&err);
    return parsed;
}

Result<void> WaitInactive(Connection& connection, const std::string& unit, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto state = ActiveState(connection, unit);
        if (!state)
            return tl::unexpected(std::move(state).error());
        // deactivated or fully unloaded both count: the caller's goal is
        // "no longer running under this unit"
        const bool left = !state->has_value() || **state == UnitState::Inactive || **state == UnitState::Failed ||
                          **state == UnitState::Unknown;
        if (left)
            return {};
        if (std::chrono::steady_clock::now() >= deadline) {
            wallpaper_engine::Error error;
            error.kind = wallpaper_engine::Error::Unknown;
            error.message = "unit " + unit + " did not leave active state within " +
                            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count()) +
                            " ms";
            return tl::unexpected(std::move(error));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

bool Tolerated(const wallpaper_engine::Error& error) noexcept {
    return error.kind == wallpaper_engine::Error::NoError || error.kind == wallpaper_engine::Error::NoSuchUnit ||
           error.message.find("not loaded") != std::string::npos;
}

} // namespace systemd
