#pragma once

#include <QDBusMessage>
#include <QString>

namespace SystemdLayer {

// Typed error for every failing operation on this layer. D-Bus error names
// are preserved for diagnostics; kind is what callers branch on.
struct Error {
    enum Kind { NoError, BusUnreachable, NoSuchUnit, JobFailed, InvalidInput, Unknown };
    Kind kind = NoError;
    QString dbusName;
    QString message;
};

// One systemd user unit and its lifecycle, backed by the
// org.freedesktop.systemd1 D-Bus API on the session bus. Knows nothing
// about wallpapers: callers feed unit text and argv. State is read by
// polling activeState(); every lifecycle call is synchronous.
class SystemdUnit {
public:
    explicit SystemdUnit(QString unitName);
    ~SystemdUnit();

    QString unitName() const;

    // Transient unit: StartTransientUnit without touching the filesystem.
    // Disappears with the session; ideal for relaunched system components.
    // extraProperties: optional additional unit properties (key -> value;
    // values marshal to their natural D-Bus types).
    bool startTransient(const QStringList& execArgs, const QMap<QString, QString>& environment,
                        const QMap<QString, QVariant>& extraProperties, Error* error = nullptr);

    bool start(Error* error = nullptr);
    bool stop(Error* error = nullptr);
    bool restart(Error* error = nullptr);
    bool resetFailed(Error* error = nullptr);

    // ActiveState per systemd: active / inactive / failed / activating.
    // A unit that is merely installed (not loaded) reads as inactive.
    QString activeState(Error* error = nullptr) const;
    bool isActive() const;

private:
    QString m_unitName;
};

// Reload the user manager so freshly written unit files are picked up.
bool daemonReload(Error* error = nullptr);

// True when the error is an acceptable outcome of an idempotent control
// operation: success, a unit that does not exist, or systemd's "not loaded"
// phrasing for the same situation.
bool tolerated(const Error& error);

} // namespace SystemdLayer
