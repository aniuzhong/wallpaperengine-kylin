#include "integration.h"

#include "systemdunit.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include <unistd.h>

namespace {

QString markerPath () {
    return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) +
           "/lwe-dynamic-wallpaper/lwe-alpha-wallpaper.png";
}

QString dataDir () {
    return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + "/lwe-dynamic-wallpaper";
}

qint64 findPeonyPid () {
    QDir proc ("/proc");
    const QStringList ids = proc.entryList (QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& id : ids) {
        bool ok = false;
        const qint64 pid = id.toLongLong (&ok);
        if (!ok || pid <= 0)
            continue;
        QFile cmd (QString ("/proc/%1/cmdline").arg (pid));
        if (!cmd.open (QIODevice::ReadOnly))
            continue;
        if (QString::fromUtf8 (cmd.readAll ()).contains ("peony-qt-desktop"))
            return pid;
    }
    return 0;
}

bool shimMapped (qint64 pid) {
    QFile maps (QString ("/proc/%1/maps").arg (pid));
    if (!maps.open (QIODevice::ReadOnly))
        return false;
    return maps.readAll ().contains ("peony-alpha-shim");
}

bool peonyGone () { return findPeonyPid () == 0; }

bool waitForPeonyExit (int timeoutMs) {
    while (timeoutMs > 0) {
        if (peonyGone ())
            return true;
        QThread::msleep (100);
        timeoutMs -= 100;
    }
    return peonyGone ();
}

QString accountUserObjectPath () {
    QProcess dbus;
    const QStringList findArgs = { "dbus-send", "--system", "--print-reply", "--dest=org.freedesktop.Accounts",
                                   "/org/freedesktop/Accounts", "org.freedesktop.Accounts.FindUserById",
                                   QString::number (static_cast<long long> (getuid ())) };
    dbus.start ("dbus-send", findArgs);
    if (!dbus.waitForFinished (5000))
        return {};
    const QString reply = QString::fromUtf8 (dbus.readAllStandardOutput ());
    const QRegularExpression re ("(/org/freedesktop/Accounts/User\\d+)");
    return re.match (reply).hasMatch () ? re.match (reply).captured (1) : QString();
}

// accountsservice normalizes (copies) the wallpaper into
// /var/lib/AccountsService/backgrounds — peony loads the normalized path at
// startup, so callers need it back.
QString getAccountBackground () {
    const QString userPath = accountUserObjectPath ();
    if (userPath.isEmpty ())
        return {};
    QProcess get;
    const QStringList getArgs = { "dbus-send", "--system", "--print-reply", "--dest=org.freedesktop.Accounts",
                                  userPath, "org.freedesktop.DBus.Properties.Get",
                                  "string:org.freedesktop.Accounts.User", "string:BackgroundFile" };
    get.start ("dbus-send", getArgs);
    if (!get.waitForFinished (5000))
        return {};
    const QString reply = QString::fromUtf8 (get.readAllStandardOutput ());
    const QRegularExpression re ("string \"([^\"]+)\"");
    return re.match (reply).hasMatch () ? re.match (reply).captured (1) : QString();
}

void setAccountBackground (const QString& marker) {
    const QString userPath = accountUserObjectPath ();
    if (userPath.isEmpty ())
        return;
    const QStringList setArgs = { "dbus-send", "--system", "--print-reply", "--dest=org.freedesktop.Accounts",
                                  userPath, "org.freedesktop.Accounts.User.SetBackgroundFile", marker };
    QProcess::execute ("dbus-send", setArgs);
}

} // namespace

namespace Integration {

Status detect () {
    Status status;
    status.peonyPid = findPeonyPid ();
    if (status.peonyPid != 0)
        status.shimLoaded = shimMapped (status.peonyPid);
    return status;
}

bool setup (QString* error) {
    // ---- 1. marker wallpaper; accountsservice and gsettings point at it.
    // The shim nullifies the pixmap at load time, so the color is irrelevant
    // (magenta makes an unshimmed desktop obvious instead of silently dark).
    QDir ().mkpath (dataDir ());
    QFile (dataDir () + "/peony-shim.log").remove (); // fresh log per setup

    // remember the pre-change wallpaper too: if the accountsservice write
    // fails, peony still loads the OLD path and the shim must match it
    const QString previousBackground = getAccountBackground ();
    const QString marker = markerPath ();
    QImage image (64, 64, QImage::Format_RGB888);
    image.fill (QColor (255, 0, 255));
    if (!image.save (marker)) {
        if (error) *error = "cannot write marker wallpaper to " + marker;
        return false;
    }

    setAccountBackground (marker);
    const QString normalized = getAccountBackground ();
    QProcess::execute ("gsettings", { "set", "org.mate.background", "picture-filename", marker });

    // peony loads the accountsservice-normalized path at startup; gsettings
    // is what switchBackground() reads on wallpaper changes. Collect every
    // candidate path — the shim matches exact paths, basenames, and anything
    // under the accountsservice store anyway.
    QString wallpaperList = marker;
    if (!normalized.isEmpty () && normalized != marker)
        wallpaperList = normalized + ":" + wallpaperList;
    if (!previousBackground.isEmpty () && !wallpaperList.contains (previousBackground))
        wallpaperList = previousBackground + ":" + wallpaperList;

    // ---- 2. stop peony and wait for a real exit; a lingering process holds
    // the single-instance lock and our injected instance would bail out.
    QProcess::execute ("pkill", { "-f", "peony-qt-desktop" });
    if (!waitForPeonyExit (3000)) {
        QProcess::execute ("pkill", { "-9", "-f", "peony-qt-desktop" });
        QThread::msleep (300);
    }

    // ---- 3. clear stale single-instance locks and relaunch with the shim
    // preloaded (systemd-run keeps it injected across crashes).
    QDir tmp ("/tmp");
    for (const QString& lock : tmp.entryList (QStringList() << "qtsingleapp-peonyq*"))
        tmp.remove (lock);

    QProcess::execute ("systemctl", { "--user", "stop", "lwe-peony" });
    QProcess::execute ("systemctl", { "--user", "reset-failed", "lwe-peony" });

    // launch through the typed systemd layer: transient unit with
    // Restart=on-failure — if peony dies, systemd restarts it WITH the
    // injection environment (structurally guaranteed self-healing)
    const QString shimPath = QCoreApplication::applicationDirPath () + "/libpeony-alpha-shim.so";
    const QString logPath = dataDir () + "/peony-shim.log";
    SystemdLayer::SystemdUnit peonyUnit ("lwe-peony");
    SystemdLayer::Error unitError;
    QMap<QString, QString> peonyEnv;
    peonyEnv.insert ("LD_PRELOAD", shimPath);
    peonyEnv.insert ("PEONY_ALPHA_WALLPAPER", wallpaperList);
    peonyEnv.insert ("PEONY_ALPHA_LOG", logPath);
    if (!peonyUnit.startTransient ({ "/usr/bin/peony-qt-desktop", "-w", "-d" }, peonyEnv, {}, &unitError)) {
        if (error) *error = "transient launch failed: " + unitError.message;
        return false;
    }

    // ---- 4. verify the shim actually mapped into the new instance
    for (int waited = 0; waited < 10000; waited += 300) {
        QThread::msleep (300);
        const qint64 pid = findPeonyPid ();
        if (pid == 0)
            continue;
        if (shimMapped (pid)) {
            if (error) error->clear ();
            return true;
        }
    }
    if (error) *error = "peony relaunched but the shim did not map (list: " + wallpaperList + "; log: " + logPath + ")";
    return false;
}

} // namespace Integration
