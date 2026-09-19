#include "systemdunit.h"
#include "unitbuilder.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QVariant>
#include <utility>

// ----------------------------------------------------------------- contracts
// Marshaling structures for StartTransientUnit. Declared at SystemdLayer
// scope (not anonymous) so the Q_DECLARE_METATYPE specializations after the
// namespace can use a nested-name-specifier, as C++ requires.
namespace SystemdLayer {

constexpr const char* kService = "org.freedesktop.systemd1";
constexpr const char* kManagerPath = "/org/freedesktop/systemd1";
constexpr const char* kManagerIface = "org.freedesktop.systemd1.Manager";
constexpr const char* kUnitIface = "org.freedesktop.systemd1.Unit";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";

// property entry: (key, variant) — signature (sv)
struct DbusProperty {
    QString key;
    QDBusVariant value;
};
using DbusPropertyList = QList<DbusProperty>;

// ExecStart entry: (path, argv, ignore-failure) — signature (sasb)
struct ExecStartEntry {
    QString program;
    QStringList argv;
    bool ignoreFailure = false;
};
using ExecStartList = QList<ExecStartEntry>;

// aux entry: (path, properties) — signature (sa(sv)); systemd 245 expects
// StartTransientUnit aux as a(sa(sv)), NOT a(sba(sv))
struct AuxEntry {
    QString path;
    DbusPropertyList properties;
};
using AuxList = QList<AuxEntry>;

QDBusArgument &operator<< (QDBusArgument &arg, const DbusProperty &p) {
    arg.beginStructure ();
    arg << p.key << p.value;
    arg.endStructure ();
    return arg;
}
const QDBusArgument &operator>> (const QDBusArgument &arg, DbusProperty &p) {
    arg.beginStructure ();
    arg >> p.key >> p.value;
    arg.endStructure ();
    return arg;
}

QDBusArgument &operator<< (QDBusArgument &arg, const ExecStartEntry &e) {
    arg.beginStructure ();
    arg << e.program << e.argv << e.ignoreFailure;
    arg.endStructure ();
    return arg;
}
const QDBusArgument &operator>> (const QDBusArgument &arg, ExecStartEntry &e) {
    arg.beginStructure ();
    arg >> e.program >> e.argv >> e.ignoreFailure;
    arg.endStructure ();
    return arg;
}

QDBusArgument &operator<< (QDBusArgument &arg, const AuxEntry &a) {
    arg.beginStructure ();
    arg << a.path << a.properties;
    arg.endStructure ();
    return arg;
}
const QDBusArgument &operator>> (const QDBusArgument &arg, AuxEntry &a) {
    arg.beginStructure ();
    arg >> a.path >> a.properties;
    arg.endStructure ();
    return arg;
}

} // namespace SystemdLayer

// The metatype declarations must sit at global scope, after the namespace,
// with the fully qualified names.
Q_DECLARE_METATYPE (SystemdLayer::DbusProperty)
Q_DECLARE_METATYPE (SystemdLayer::DbusPropertyList)
Q_DECLARE_METATYPE (SystemdLayer::ExecStartEntry)
Q_DECLARE_METATYPE (SystemdLayer::ExecStartList)
Q_DECLARE_METATYPE (SystemdLayer::AuxEntry)
Q_DECLARE_METATYPE (SystemdLayer::AuxList)

namespace SystemdLayer {

void registerDBusTypes () {
    static bool registered = false;
    if (registered)
        return;
    qDBusRegisterMetaType<DbusProperty> ();
    qDBusRegisterMetaType<DbusPropertyList> ();
    qDBusRegisterMetaType<ExecStartEntry> ();
    qDBusRegisterMetaType<ExecStartList> ();
    qDBusRegisterMetaType<AuxEntry> ();
    qDBusRegisterMetaType<AuxList> ();
    registered = true;
}

QDBusMessage callManager (const QString& method, const QVariantList& args, Error* error) {
    QDBusMessage call = QDBusMessage::createMethodCall (kService, kManagerPath, kManagerIface, method);
    call.setArguments (args);
    const QDBusMessage reply = QDBusConnection::sessionBus ().call (call);

    if (reply.type () == QDBusMessage::ErrorMessage) {
        if (error) {
            error->dbusName = reply.errorName ();
            error->message = reply.errorMessage ();
            error->kind = reply.errorName ().contains ("NoSuchUnit") ? Error::NoSuchUnit : Error::Unknown;
        }
        return {};
    }
    if (error) {
        error->kind = Error::NoError;
        error->dbusName.clear ();
        error->message.clear ();
    }
    return reply;
}

bool daemonReload (Error* error) {
    Error local;
    callManager ("Reload", {}, &local);
    if (error) *error = local;
    return local.kind == Error::NoError;
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

SystemdUnit::SystemdUnit (QString unitName, QObject* parent) : QObject (parent), m_unitName (std::move (unitName)) {
    // accept a bare name the way systemctl does: the D-Bus manager requires
    // a full unit id with the type suffix, so "linux-wallpaperengine"
    // becomes "linux-wallpaperengine.service"
    if (!this->m_unitName.contains ('.'))
        this->m_unitName += ".service";
    registerDBusTypes ();
}

SystemdUnit::~SystemdUnit () {}

QString SystemdUnit::unitName () const { return m_unitName; }

void SystemdUnit::subscribe () {
    if (m_subscribed)
        return;
    // keep the manager emitting unit signals for this client
    callManager ("Subscribe", {}, nullptr);
    QDBusConnection::sessionBus ().connect (kService, unitObjectPathFromId (m_unitName), kPropsIface,
                                            "PropertiesChanged", this,
                                            SLOT (onPropertiesChanged (QDBusMessage)));
    m_subscribed = true;
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

    // a stale failed unit with the same name blocks re-creation ("already
    // exists") — clear it first; a not-loaded unit is a harmless no-op
    callManager ("ResetFailedUnit", { m_unitName }, nullptr);

    DbusPropertyList properties;
    properties.append ({ "Description", QDBusVariant ("wallpaper transient unit") });
    properties.append ({ "Type", QDBusVariant ("simple") });
    properties.append ({ "Restart", QDBusVariant ("on-failure") });
    for (auto it = extraProperties.begin (); it != extraProperties.end (); ++it)
        properties.append ({ it.key (), QDBusVariant (it.value ()) });

    const ExecStartEntry entry { command.program, command.args, false };
    properties.append ({ "ExecStart", QDBusVariant (QVariant::fromValue (ExecStartList { entry })) });

    const AuxList aux {}; // empty aux; element signature (sba(sv)) via registered type

    subscribe ();
    const QDBusMessage reply = callManager ("StartTransientUnit",
                                            { m_unitName, "replace", QVariant::fromValue (properties),
                                              QVariant::fromValue (aux) },
                                            error);
    return error == nullptr || error->kind == Error::NoError;
}

bool SystemdUnit::start (Error* error) {
    subscribe (); // before the operation: the change signal must not be missed
    Error local;
    const QDBusMessage reply = callManager ("StartUnit", { m_unitName, "replace" }, &local);
    if (error) *error = local;
    return reply.type () == QDBusMessage::ReplyMessage;
}

bool SystemdUnit::stop (Error* error) {
    Error local;
    const QDBusMessage reply = callManager ("StopUnit", { m_unitName, "replace" }, &local);
    if (error) *error = local;
    return reply.type () == QDBusMessage::ReplyMessage;
}

bool SystemdUnit::restart (Error* error) {
    subscribe ();
    Error local;
    const QDBusMessage reply = callManager ("RestartUnit", { m_unitName, "replace" }, &local);
    if (error) *error = local;
    return reply.type () == QDBusMessage::ReplyMessage;
}

bool SystemdUnit::resetFailed (Error* error) {
    // advisory operation: a unit that is not loaded has nothing to reset —
    // systemd errors on it, but callers mean "make sure it can start", so
    // that outcome is success
    Error local;
    callManager ("ResetFailedUnit", { m_unitName }, &local);
    if (tolerated (local))
        local = {};
    if (error) *error = local;
    return local.kind == Error::NoError;
}

QString SystemdUnit::activeState (Error* error) const {
    QDBusMessage get = QDBusMessage::createMethodCall (kService, unitObjectPathFromId (m_unitName), kPropsIface,
                                                       "Get");
    get.setArguments ({ kUnitIface, "ActiveState" });
    const QDBusMessage reply = QDBusConnection::sessionBus ().call (get);

    if (reply.type () == QDBusMessage::ErrorMessage) {
        // a known unit that is not loaded yet reads as inactive
        if (reply.errorName ().contains ("UnknownInterface") || reply.errorName ().contains ("NoSuchUnit"))
            return "inactive";
        if (error) {
            error->dbusName = reply.errorName ();
            error->message = reply.errorMessage ();
            error->kind = Error::Unknown;
        }
        return "unknown";
    }
    if (reply.arguments ().isEmpty ())
        return "unknown";
    const QDBusVariant variant = reply.arguments ().first ().value<QDBusVariant> ();
    return variant.variant ().toString ();
}

bool SystemdUnit::isActive () const { return activeState () == "active"; }

void SystemdUnit::onPropertiesChanged (const QDBusMessage &message) {
    if (message.arguments ().size () < 2)
        return;
    const QDBusArgument changed = message.arguments ().at (1).value<QDBusArgument> ();
    changed.beginArray ();
    while (!changed.atEnd ()) {
        changed.beginStructure ();
        QString key;
        QDBusVariant value;
        changed >> key >> value;
        changed.endStructure ();
        if (key == "ActiveState")
            emit stateChanged (value.variant ().toString ());
    }
    changed.endArray ();
}

} // namespace SystemdLayer
