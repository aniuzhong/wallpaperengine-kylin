#include "systemdunit.h"
#include "unitbuilder.h"

#include <QVariant>
#include <utility>

#include <systemd/sd-bus.h>

#include <cstdarg>
#include <cstdlib>
#include <cstring>

// The systemd user manager addressed through the sd-bus C API directly —
// no QtDBus in this layer. One operation = one bus connection = one round
// trip; the manager holds no per-client state worth keeping a connection
// alive for (the previous live-change subscription was removed as dead
// code; state is polled through activeState()).

namespace SystemdLayer {

constexpr const char* kService = "org.freedesktop.systemd1";
constexpr const char* kManagerPath = "/org/freedesktop/systemd1";
constexpr const char* kManagerIface = "org.freedesktop.systemd1.Manager";
constexpr const char* kUnitIface = "org.freedesktop.systemd1.Unit";

// Connect to the user bus; returns nullptr with BusUnreachable set on failure.
sd_bus* openUserBus (Error* error) {
    sd_bus* bus = nullptr;
    if (sd_bus_open_user (&bus) < 0) {
        if (error) {
            error->kind = Error::BusUnreachable;
            error->message = QStringLiteral ("cannot connect to the user bus");
        }
        return nullptr;
    }
    return bus;
}

// Map a failed sd-bus call onto the typed error. D-Bus error names are
// preserved for diagnostics; kind is what callers branch on.
void takeError (int rc, const sd_bus_error& err, Error* error) {
    if (error == nullptr)
        return;
    if (err.name != nullptr) {
        error->dbusName = QString::fromUtf8 (err.name);
        error->message = QString::fromUtf8 (err.message);
        error->kind = std::strstr (err.name, "NoSuchUnit") != nullptr ? Error::NoSuchUnit : Error::Unknown;
    } else {
        error->kind = Error::Unknown;
        error->message = QString::fromUtf8 (std::strerror (-rc));
    }
}

// Fire one manager method (varargs-encoded arguments) and report success.
bool callManager (const char* method, const char* types, Error* error, ...) {
    va_list ap;
    va_start (ap, error);
    sd_bus* bus = openUserBus (error);
    if (bus == nullptr) {
        va_end (ap);
        return false;
    }

    sd_bus_message* m = nullptr;
    int rc = sd_bus_message_new_method_call (bus, &m, kService, kManagerPath, kManagerIface, method);
    if (rc >= 0)
        rc = sd_bus_message_appendv (m, types, ap);
    va_end (ap);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (rc >= 0)
        rc = sd_bus_call (bus, m, 0, &err, &reply);

    const bool ok = rc >= 0;
    if (!ok)
        takeError (rc, err, error);
    sd_bus_message_unref (m);
    sd_bus_message_unref (reply);
    sd_bus_error_free (&err);
    sd_bus_flush_close_unref (bus);
    return ok;
}

bool daemonReload (Error* error) {
    return callManager ("Reload", "", error);
}

bool tolerated (const Error& error) {
    return error.kind == Error::NoError || error.kind == Error::NoSuchUnit ||
           error.message.contains ("not loaded");
}

QString unitObjectPathFromId (const QString& unitId) {
    // systemd escapes every non [A-Za-z0-9] byte of the unit id as _XX
    QString escaped;
    for (const char c : unitId.toLatin1 ()) {
        const unsigned char u = static_cast<unsigned char> (c);
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9'))
            escaped += c;
        else
            escaped += QString ("_%1").arg (u, 2, 16, QLatin1Char ('0'));
    }
    return QString ("/org/freedesktop/systemd1/unit/%1").arg (escaped);
}

SystemdUnit::SystemdUnit (QString unitName) : m_unitName (std::move (unitName)) {
    // accept a bare name the way systemctl does: the D-Bus manager requires
    // a full unit id with the type suffix, so "linux-wallpaperengine"
    // becomes "linux-wallpaperengine.service"
    if (!this->m_unitName.contains ('.'))
        this->m_unitName += ".service";
}

SystemdUnit::~SystemdUnit () {}

QString SystemdUnit::unitName () const { return m_unitName; }

// Marshal one caller-chosen scalar into an in-message variant; string is the
// fallback — nothing in this codebase passes exotic property types.
int appendQVariant (sd_bus_message* m, const QVariant& value) {
    switch (value.type ()) {
    case QVariant::Bool: return sd_bus_message_append (m, "v", "b", int (value.toBool ()));
    case QVariant::Int: return sd_bus_message_append (m, "v", "i", value.toInt ());
    case QVariant::UInt: return sd_bus_message_append (m, "v", "u", value.toUInt ());
    case QVariant::LongLong: return sd_bus_message_append (m, "v", "x", value.toLongLong ());
    case QVariant::Double: return sd_bus_message_append (m, "v", "d", value.toDouble ());
    default: return sd_bus_message_append (m, "v", "s", value.toString ().toUtf8 ().constData ());
    }
}

// Append the ExecStart=(sasb) property: one entry carrying the full argv,
// failure tolerance off.
int appendExecStart (sd_bus_message* m, const ExecCommand& command) {
    int rc = sd_bus_message_open_container (m, SD_BUS_TYPE_STRUCT, "sv");
    if (rc >= 0) rc = sd_bus_message_append (m, "s", "ExecStart");
    if (rc >= 0) rc = sd_bus_message_open_container (m, SD_BUS_TYPE_VARIANT, "a(sasb)");
    if (rc >= 0) rc = sd_bus_message_open_container (m, SD_BUS_TYPE_ARRAY, "(sasb)");
    if (rc >= 0) rc = sd_bus_message_open_container (m, SD_BUS_TYPE_STRUCT, "sasb");
    if (rc >= 0) rc = sd_bus_message_append (m, "s", command.program.toUtf8 ().constData ());
    if (rc >= 0) rc = sd_bus_message_open_container (m, SD_BUS_TYPE_ARRAY, "s");
    for (const QString& arg : command.args) {
        if (rc < 0)
            break;
        const QByteArray utf8 = arg.toUtf8 ();
        rc = sd_bus_message_append (m, "s", utf8.constData ());
    }
    if (rc >= 0) rc = sd_bus_message_close_container (m); // argv
    if (rc >= 0) rc = sd_bus_message_append (m, "b", 0);  // ignore-failure flag
    if (rc >= 0) rc = sd_bus_message_close_container (m); // (sasb)
    if (rc >= 0) rc = sd_bus_message_close_container (m); // a(sasb)
    if (rc >= 0) rc = sd_bus_message_close_container (m); // variant v
    if (rc >= 0) rc = sd_bus_message_close_container (m); // struct (sv)
    return rc;
}

bool SystemdUnit::startTransient (const QStringList& execArgs, const QMap<QString, QString>& environment,
                                  const QMap<QString, QVariant>& extraProperties, Error* error) {
    if (execArgs.isEmpty ()) {
        if (error) {
            error->kind = Error::InvalidInput;
            error->message = "empty argv";
        }
        return false;
    }

    // environment via /usr/bin/env prefix: systemd's transient Environment
    // property is not reliably applied to the exec'd process on all versions
    QStringList finalArgs { "/usr/bin/env" };
    for (auto it = environment.begin (); it != environment.end (); ++it)
        finalArgs << it.key () + "=" + it.value ();
    finalArgs << execArgs;

    const ExecCommand command = toExecCommand (finalArgs);

    sd_bus* bus = openUserBus (error);
    if (bus == nullptr)
        return false;

    // a stale failed unit with the same name blocks re-creation ("already
    // exists") — clear it first; a not-loaded unit is a harmless no-op
    sd_bus_call_method (bus, kService, kManagerPath, kManagerIface, "ResetFailedUnit", nullptr, nullptr, "s",
                        m_unitName.toUtf8 ().constData ());

    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call (bus, &m, kService, kManagerPath, kManagerIface, "StartTransientUnit") < 0)
        m = nullptr;

    int rc = -1;
    if (m != nullptr) rc = sd_bus_message_append (m, "ss", m_unitName.toUtf8 ().constData (), "replace");
    if (rc >= 0) rc = sd_bus_message_open_container (m, SD_BUS_TYPE_ARRAY, "(sv)");
    if (rc >= 0) rc = sd_bus_message_append (m, "(sv)", "Description", "s", "wallpaper transient unit");
    if (rc >= 0) rc = sd_bus_message_append (m, "(sv)", "Type", "s", "simple");
    if (rc >= 0) rc = sd_bus_message_append (m, "(sv)", "Restart", "s", "on-failure");
    for (auto it = extraProperties.begin (); rc >= 0 && it != extraProperties.end (); ++it) {
        rc = sd_bus_message_open_container (m, SD_BUS_TYPE_STRUCT, "sv");
        if (rc >= 0) rc = sd_bus_message_append (m, "s", it.key ().toUtf8 ().constData ());
        if (rc >= 0) rc = appendQVariant (m, it.value ());
        if (rc >= 0) rc = sd_bus_message_close_container (m);
    }
    if (rc >= 0) rc = appendExecStart (m, command);
    if (rc >= 0) rc = sd_bus_message_close_container (m); // properties a(sv)
    if (rc >= 0) rc = sd_bus_message_open_container (m, SD_BUS_TYPE_ARRAY, "(sa(sv))");
    if (rc >= 0) rc = sd_bus_message_close_container (m); // aux: empty — systemd 245 expects a(sa(sv)),
                                                          // NOT the upstream-documented a(sba(sv))

    bool ok = false;
    if (rc >= 0) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        rc = sd_bus_call (bus, m, 0, &err, &reply);
        sd_bus_message_unref (reply);
        ok = rc >= 0;
        if (!ok)
            takeError (rc, err, error);
        sd_bus_error_free (&err);
    } else if (error != nullptr) {
        error->kind = Error::Unknown;
        error->message = QString::fromUtf8 (std::strerror (-rc));
    }
    sd_bus_message_unref (m);
    sd_bus_flush_close_unref (bus);
    return ok;
}

bool SystemdUnit::start (Error* error) {
    return callManager ("StartUnit", "ss", error, m_unitName.toUtf8 ().constData (), "replace");
}

bool SystemdUnit::stop (Error* error) {
    return callManager ("StopUnit", "ss", error, m_unitName.toUtf8 ().constData (), "replace");
}

bool SystemdUnit::restart (Error* error) {
    return callManager ("RestartUnit", "ss", error, m_unitName.toUtf8 ().constData (), "replace");
}

bool SystemdUnit::resetFailed (Error* error) {
    // advisory operation: a unit that is not loaded has nothing to reset —
    // systemd errors on it, but callers mean "make sure it can start", so
    // that outcome is success
    Error local;
    callManager ("ResetFailedUnit", "s", &local, m_unitName.toUtf8 ().constData ());
    if (tolerated (local))
        local = {};
    if (error) *error = local;
    return local.kind == Error::NoError;
}

QString SystemdUnit::activeState (Error* error) const {
    sd_bus* bus = openUserBus (error);
    if (bus == nullptr)
        return QStringLiteral ("unknown");

    sd_bus_error err = SD_BUS_ERROR_NULL;
    char* state = nullptr;
    const QByteArray path = unitObjectPathFromId (m_unitName).toUtf8 ();
    const int rc = sd_bus_get_property_string (bus, kService, path.constData (), kUnitIface, "ActiveState", &err,
                                               &state);

    QString result;
    if (rc < 0) {
        // a known unit that is not loaded yet reads as inactive
        if (err.name != nullptr &&
            (std::strstr (err.name, "UnknownInterface") != nullptr || std::strstr (err.name, "NoSuchUnit") != nullptr))
            result = QStringLiteral ("inactive");
        else {
            result = QStringLiteral ("unknown");
            takeError (rc, err, error);
        }
    } else {
        result = QString::fromUtf8 (state != nullptr ? state : "unknown");
    }
    free (state);
    sd_bus_error_free (&err);
    sd_bus_flush_close_unref (bus);
    return result;
}

bool SystemdUnit::isActive () const { return activeState () == "active"; }

} // namespace SystemdLayer
