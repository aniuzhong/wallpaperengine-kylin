#include "systemdunit.h"
#include "unitbuilder.h"

#include <systemd/sd-bus.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

// The systemd user manager addressed through the sd-bus C API directly.
// One operation = one bus connection = one round trip; the manager holds no
// per-client state worth keeping a connection alive for (the previous
// live-change subscription was removed as dead code; state is polled
// through activeState()).

namespace SystemdLayer {

constexpr const char* kService = "org.freedesktop.systemd1";
constexpr const char* kManagerPath = "/org/freedesktop/systemd1";
constexpr const char* kManagerIface = "org.freedesktop.systemd1.Manager";
constexpr const char* kUnitIface = "org.freedesktop.systemd1.Unit";

// Connect to the user bus; returns nullptr with BusUnreachable set on failure.
sd_bus* openUserBus(Error* error) {
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
        if (error) {
            error->kind = Error::BusUnreachable;
            error->message = "cannot connect to the user bus";
        }
        return nullptr;
    }
    return bus;
}

// Map a failed sd-bus call onto the typed error. D-Bus error names are
// preserved for diagnostics; kind is what callers branch on.
void takeError(int rc, const sd_bus_error& err, Error* error) {
    if (error == nullptr)
        return;
    if (err.name != nullptr) {
        error->dbusName = err.name;
        error->message = err.message != nullptr ? err.message : "";
        error->kind = std::strstr(err.name, "NoSuchUnit") != nullptr ? Error::NoSuchUnit : Error::Unknown;
    } else {
        error->kind = Error::Unknown;
        error->message = std::strerror(-rc);
    }
}

// Fire one manager method (varargs-encoded arguments) and report success.
bool callManager(const char* method, const char* types, Error* error, ...) {
    va_list ap;
    va_start(ap, error);
    sd_bus* bus = openUserBus(error);
    if (bus == nullptr) {
        va_end(ap);
        return false;
    }

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
        takeError(rc, err, error);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    sd_bus_flush_close_unref(bus);
    return ok;
}

bool daemonReload(Error* error) {
    return callManager("Reload", "", error);
}

bool tolerated(const Error& error) {
    return error.kind == Error::NoError || error.kind == Error::NoSuchUnit ||
           error.message.find("not loaded") != std::string::npos;
}

std::string unitObjectPathFromId(const std::string& unitId) {
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

SystemdUnit::SystemdUnit(std::string unitName) : m_unitName(std::move(unitName)) {
    // accept a bare name the way systemctl does: the D-Bus manager requires
    // a full unit id with the type suffix, so "wallpaper-engine"
    // becomes "wallpaper-engine.service"
    if (m_unitName.find('.') == std::string::npos)
        m_unitName += ".service";
}

SystemdUnit::~SystemdUnit() {}

std::string SystemdUnit::unitName() const { return m_unitName; }

// Marshal one caller-chosen scalar into an in-message variant.
int appendPropertyValue(sd_bus_message* m, const UnitPropertyValue& value) {
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
int appendExecStart(sd_bus_message* m, const ExecCommand& command) {
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

bool SystemdUnit::startTransient(const std::vector<std::string>& execArgs,
                                  const std::map<std::string, std::string>& environment,
                                  const std::map<std::string, UnitPropertyValue>& extraProperties, Error* error) {
    if (execArgs.empty()) {
        if (error) {
            error->kind = Error::InvalidInput;
            error->message = "empty argv";
        }
        return false;
    }

    // environment via /usr/bin/env prefix: systemd's transient Environment
    // property is not reliably applied to the exec'd process on all versions
    std::vector<std::string> finalArgs { "/usr/bin/env" };
    for (const auto& [key, value] : environment)
        finalArgs.push_back(key + "=" + value);
    finalArgs.insert(finalArgs.end(), execArgs.begin(), execArgs.end());

    const ExecCommand command = toExecCommand(finalArgs);

    sd_bus* bus = openUserBus(error);
    if (bus == nullptr)
        return false;

    // a stale failed unit with the same name blocks re-creation ("already
    // exists") — clear it first; a not-loaded unit is a harmless no-op
    sd_bus_call_method(bus, kService, kManagerPath, kManagerIface, "ResetFailedUnit", nullptr, nullptr, "s",
                        m_unitName.c_str());

    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(bus, &m, kService, kManagerPath, kManagerIface, "StartTransientUnit") < 0)
        m = nullptr;

    int rc = -1;
    if (m != nullptr) rc = sd_bus_message_append(m, "ss", m_unitName.c_str(), "replace");
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "(sv)");
    if (rc >= 0) rc = sd_bus_message_append(m, "(sv)", "Description", "s", "wallpaper transient unit");
    if (rc >= 0) rc = sd_bus_message_append(m, "(sv)", "Type", "s", "simple");
    if (rc >= 0) rc = sd_bus_message_append(m, "(sv)", "Restart", "s", "on-failure");
    for (const auto& [key, value] : extraProperties) {
        if (rc < 0)
            break;
        rc = sd_bus_message_open_container(m, SD_BUS_TYPE_STRUCT, "sv");
        if (rc >= 0) rc = sd_bus_message_append(m, "s", key.c_str());
        if (rc >= 0) rc = appendPropertyValue(m, value);
        if (rc >= 0) rc = sd_bus_message_close_container(m);
    }
    if (rc >= 0) rc = appendExecStart(m, command);
    if (rc >= 0) rc = sd_bus_message_close_container(m); // properties a(sv)
    if (rc >= 0) rc = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "(sa(sv))");
    if (rc >= 0) rc = sd_bus_message_close_container(m); // aux: empty — systemd 245 expects a(sa(sv)),
                                                          // NOT the upstream-documented a(sba(sv))

    bool ok = false;
    if (rc >= 0) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        rc = sd_bus_call(bus, m, 0, &err, &reply);
        sd_bus_message_unref(reply);
        ok = rc >= 0;
        if (!ok)
            takeError(rc, err, error);
        sd_bus_error_free(&err);
    } else if (error != nullptr) {
        error->kind = Error::Unknown;
        error->message = std::strerror(-rc);
    }
    sd_bus_message_unref(m);
    sd_bus_flush_close_unref(bus);
    return ok;
}

bool SystemdUnit::start(Error* error) {
    return callManager("StartUnit", "ss", error, m_unitName.c_str(), "replace");
}

bool SystemdUnit::stop(Error* error) {
    return callManager("StopUnit", "ss", error, m_unitName.c_str(), "replace");
}

bool SystemdUnit::restart(Error* error) {
    return callManager("RestartUnit", "ss", error, m_unitName.c_str(), "replace");
}

bool SystemdUnit::resetFailed(Error* error) {
    // advisory operation: a unit that is not loaded has nothing to reset —
    // systemd errors on it, but callers mean "make sure it can start", so
    // that outcome is success
    Error local;
    callManager("ResetFailedUnit", "s", &local, m_unitName.c_str());
    if (tolerated(local))
        local = {};
    if (error) *error = local;
    return local.kind == Error::NoError;
}

std::string SystemdUnit::activeState(Error* error) const {
    sd_bus* bus = openUserBus(error);
    if (bus == nullptr)
        return "unknown";

    sd_bus_error err = SD_BUS_ERROR_NULL;
    char* state = nullptr;
    const int rc = sd_bus_get_property_string(bus, kService, unitObjectPathFromId(m_unitName).c_str(), kUnitIface,
                                               "ActiveState", &err, &state);

    std::string result;
    if (rc < 0) {
        // a known unit that is not loaded yet reads as inactive
        if (err.name != nullptr &&
            (std::strstr(err.name, "UnknownInterface") != nullptr || std::strstr(err.name, "NoSuchUnit") != nullptr))
            result = "inactive";
        else {
            result = "unknown";
            takeError(rc, err, error);
        }
    } else {
        result = state != nullptr ? state : "unknown";
    }
    free(state);
    sd_bus_error_free(&err);
    sd_bus_flush_close_unref(bus);
    return result;
}

bool SystemdUnit::isActive() const { return activeState() == "active"; }

} // namespace SystemdLayer
