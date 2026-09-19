#pragma once

#include <QDBusMessage>
#include <QObject>
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
// about wallpapers: callers feed unit text and argv.
class SystemdUnit : public QObject {
    Q_OBJECT
public:
    explicit SystemdUnit (QString unitName, QObject* parent = nullptr);
    ~SystemdUnit () override;

    QString unitName () const;

    // Persistent unit: write the file to ~/.config/systemd/user/<unit> and
    // daemon-reload so the manager picks it up.
    bool installUnitFile (const QString& content, Error* error = nullptr);
    bool removeUnitFile (Error* error = nullptr);

    // Transient unit: StartTransientUnit without touching the filesystem.
    // Disappears with the session; ideal for relaunched system components.
    // extraProperties: optional additional unit properties (key -> value;
    // values marshal to their natural D-Bus types).
    bool startTransient (const QStringList& execArgs, const QMap<QString, QString>& environment,
                         const QMap<QString, QVariant>& extraProperties, Error* error = nullptr);

    bool start (Error* error = nullptr);
    bool stop (Error* error = nullptr);
    bool restart (Error* error = nullptr);
    bool resetFailed (Error* error = nullptr);

    // ActiveState per systemd: active / inactive / failed / activating.
    // A unit that is merely installed (not loaded) reads as inactive.
    QString activeState (Error* error = nullptr) const;
    bool isActive () const;

signals:
    // Emitted when the manager reports an ActiveState change for this unit.
    void stateChanged (const QString& activeState);

private slots:
    void onPropertiesChanged (const QDBusMessage& message);

private:
    void subscribe ();
    static QString escapeUnitId (const QString& unitId);

    QString m_unitName;
    bool m_subscribed = false;
};

// Reload the user manager so freshly written unit files are picked up.
bool daemonReload (Error* error = nullptr);

} // namespace SystemdLayer
