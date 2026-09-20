#include "integration.h"

#include "systemdunit.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include <png.h>

#include <setjmp.h>
#include <sys/stat.h>
#include <csignal>
#include <unistd.h>

namespace {

QString markerPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           "/lwe-dynamic-wallpaper/lwe-alpha-wallpaper.png";
}

QString dataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/lwe-dynamic-wallpaper";
}

QList<qint64> findPeonyPids() {
    QList<qint64> pids;
    QDir proc("/proc");
    const QStringList ids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& id : ids) {
        bool ok = false;
        const qint64 pid = id.toLongLong(&ok);
        if (!ok || pid <= 0)
            continue;
        // only this user's desktop: another session's peony is not ours to
        // touch (a command-line match system-wide would kill it)
        struct stat st;
        if (::stat(QString("/proc/%1").arg(pid).toUtf8().constData(), &st) != 0 || st.st_uid != getuid())
            continue;
        QFile cmd(QString("/proc/%1/cmdline").arg(pid));
        if (!cmd.open(QIODevice::ReadOnly))
            continue;
        if (QString::fromUtf8(cmd.readAll()).contains("peony-qt-desktop"))
            pids.append(pid);
    }
    return pids;
}

qint64 findPeonyPid() {
    const QList<qint64> pids = findPeonyPids();
    return pids.isEmpty() ? 0 : pids.first();
}

bool shimMapped(qint64 pid) {
    QFile maps(QString("/proc/%1/maps").arg(pid));
    if (!maps.open(QIODevice::ReadOnly))
        return false;
    return maps.readAll().contains("peony-alpha-shim");
}

bool peonyGone() {
    return findPeonyPids().isEmpty();
}

bool waitForPeonyExit(int timeoutMs) {
    while (timeoutMs > 0) {
        if (peonyGone())
            return true;
        QThread::msleep(100);
        timeoutMs -= 100;
    }
    return peonyGone();
}

QString accountUserObjectPath() {
    QProcess dbus;
    const QStringList findArgs = { "dbus-send", "--system", "--print-reply", "--dest=org.freedesktop.Accounts",
                                   "/org/freedesktop/Accounts", "org.freedesktop.Accounts.FindUserById",
                                   QString::number(static_cast<long long>(getuid())) };
    dbus.start("dbus-send", findArgs);
    if (!dbus.waitForFinished(5000))
        return {};
    const QString reply = QString::fromUtf8(dbus.readAllStandardOutput());
    const QRegularExpression re("(/org/freedesktop/Accounts/User\\d+)");
    return re.match(reply).hasMatch() ? re.match(reply).captured(1) : QString();
}

// accountsservice normalizes (copies) the wallpaper into
// /var/lib/AccountsService/backgrounds — peony loads the normalized path at
// startup, so callers need it back.
QString getAccountBackground() {
    const QString userPath = accountUserObjectPath();
    if (userPath.isEmpty())
        return {};
    QProcess get;
    const QStringList getArgs = { "dbus-send", "--system", "--print-reply", "--dest=org.freedesktop.Accounts",
                                  userPath, "org.freedesktop.DBus.Properties.Get",
                                  "string:org.freedesktop.Accounts.User", "string:BackgroundFile" };
    get.start("dbus-send", getArgs);
    if (!get.waitForFinished(5000))
        return {};
    const QString reply = QString::fromUtf8(get.readAllStandardOutput());
    const QRegularExpression re("string \"([^\"]+)\"");
    return re.match(reply).hasMatch() ? re.match(reply).captured(1) : QString();
}

void setAccountBackground(const QString& marker) {
    const QString userPath = accountUserObjectPath();
    if (userPath.isEmpty())
        return;
    const QStringList setArgs = { "dbus-send", "--system", "--print-reply", "--dest=org.freedesktop.Accounts",
                                  userPath, "org.freedesktop.Accounts.User.SetBackgroundFile", marker };
    QProcess::execute("dbus-send", setArgs);
}

// Write the marker wallpaper: 64x64 solid magenta RGB. The pixels are
// irrelevant to the design (the shim nullifies the image at load time);
// magenta only makes an unshimmed desktop obvious instead of silently dark.
// libpng is guaranteed on the target — freetype itself links it.
bool writeMarkerPng(const QString& path) {
    constexpr int kSize = 64;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr)
        return false;
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_write_struct(&png, nullptr);
        return false;
    }

    QFile file(path);
    // libpng reports errors through longjmp back into this point
    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        png_destroy_write_struct(&png, &info);
        return false;
    }

    png_set_write_fn(png, &file, [](png_structp p, png_bytep data, png_size_t length) {
        QFile* out = static_cast<QFile*>(png_get_io_ptr(p));
        if (out->write(reinterpret_cast<const char*>(data), qint64(length)) != qint64(length))
            png_error(p, "marker png: short write");
    }, nullptr);

    png_set_IHDR(png, info, kSize, kSize, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    png_byte row[kSize * 3];
    for (int x = 0; x < kSize; ++x) {
        row[x * 3 + 0] = 0xff; // magenta: full red and blue
        row[x * 3 + 1] = 0x00;
        row[x * 3 + 2] = 0xff;
    }
    for (int y = 0; y < kSize; ++y)
        png_write_row(png, row);

    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return true;
}

} // namespace

namespace Integration {

Status detect() {
    Status status;
    status.peonyPid = findPeonyPid();
    if (status.peonyPid != 0)
        status.shimLoaded = shimMapped(status.peonyPid);
    return status;
}

QString locateShim() {
    const QString name = QStringLiteral("libpeony-alpha-shim.so");
    QStringList candidates;
    if (QCoreApplication::instance() != nullptr) {
        const QString appDir = QCoreApplication::applicationDirPath();
        candidates << appDir + "/" + name          // build tree, sibling of the frontend
                   << appDir + "/../lib/" + name   // install() layout: bin/ + lib/
                   << appDir + "/../lib64/" + name;
    }
    candidates << "/usr/lib/" + name
               << "/usr/local/lib/" + name
               << "/usr/lib/x86_64-linux-gnu/" + name;
    for (const QString& candidate : candidates)
        if (QFile::exists(candidate))
            return candidate;
    return QString();
}

bool setup(QString* error) {
    // ---- 1. marker wallpaper; accountsservice and gsettings point at it.
    // The shim nullifies the pixmap at load time, so the color is irrelevant
    // (magenta makes an unshimmed desktop obvious instead of silently dark).
    QDir().mkpath(dataDir());
    QFile(dataDir() + "/peony-shim.log").remove(); // fresh log per setup

    const QString marker = markerPath();
    if (!writeMarkerPng(marker)) {
        if (error)
            *error = "cannot write marker wallpaper to " + marker;
        return false;
    }

    // remember the pre-change wallpaper too: if the accountsservice write
    // fails, peony still loads the OLD path and the shim must match it
    const QString previousBackground = getAccountBackground();

    setAccountBackground(marker);
    const QString normalized = getAccountBackground();
    QProcess::execute("gsettings", { "set", "org.mate.background", "picture-filename", marker });

    // peony loads the accountsservice-normalized path at startup; gsettings
    // is what switchBackground() reads on wallpaper changes. Collect every
    // candidate path — the shim matches exact paths, basenames, and anything
    // under the accountsservice store anyway.
    QString wallpaperList = marker;
    if (!normalized.isEmpty() && normalized != marker)
        wallpaperList = normalized + ":" + wallpaperList;
    if (!previousBackground.isEmpty() && !wallpaperList.contains(previousBackground))
        wallpaperList = previousBackground + ":" + wallpaperList;

    // ---- 2. stop peony and wait for a real exit; a lingering process holds
    // the single-instance lock and our injected instance would bail out.
    for (const qint64 pid : findPeonyPids())
        ::kill(pid, SIGTERM);
    if (!waitForPeonyExit(3000)) {
        for (const qint64 pid : findPeonyPids())
            ::kill(pid, SIGKILL);
        QThread::msleep(300);
    }

    // ---- 3. clear stale single-instance locks and relaunch with the shim
    // preloaded (systemd-run keeps it injected across crashes).
    QDir tmp("/tmp");
    for (const QString& lock : tmp.entryList(QStringList() << "qtsingleapp-peonyq*"))
        tmp.remove(lock);

    // launch through the typed systemd layer: transient unit with
    // Restart=on-failure — if peony dies, systemd restarts it WITH the
    // injection environment (structurally guaranteed self-healing)
    SystemdLayer::SystemdUnit peonyUnit("linux-wallpaperengine-peony");
    // a stale failed unit with the same name blocks re-creation — clear it
    // first; both calls are no-ops when nothing is loaded
    peonyUnit.stop();
    peonyUnit.resetFailed();

    const QString shimPath = locateShim();
    if (shimPath.isEmpty()) {
        if (error)
            *error = QString("libpeony-alpha-shim.so not found next to the frontend or in the standard "
                             "library paths (looked in %1)")
                         .arg(QCoreApplication::applicationDirPath());
        return false;
    }
    const QString logPath = dataDir() + "/peony-shim.log";
    SystemdLayer::Error unitError;
    QMap<QString, QString> peonyEnv;
    peonyEnv.insert("LD_PRELOAD", shimPath);
    peonyEnv.insert("PEONY_ALPHA_WALLPAPER", wallpaperList);
    peonyEnv.insert("PEONY_ALPHA_LOG", logPath);
    if (!peonyUnit.startTransient({ "/usr/bin/peony-qt-desktop", "-w", "-d" }, peonyEnv, {}, &unitError)) {
        if (error)
            *error = "transient launch failed: " + unitError.message;
        return false;
    }

    // ---- 4. verify the shim actually mapped into the new instance
    for (int waited = 0; waited < 10000; waited += 300) {
        QThread::msleep(300);
        const qint64 pid = findPeonyPid();
        if (pid == 0)
            continue;
        if (shimMapped(pid)) {
            if (error)
                error->clear();
            return true;
        }
    }
    if (error)
        *error = "peony relaunched but the shim did not map (list: " + wallpaperList + "; log: " + logPath + ")";
    return false;
}

} // namespace Integration
