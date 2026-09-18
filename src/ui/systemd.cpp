#include "systemd.h"

#include "argvbuilder.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace {

constexpr const char* kUnitName = "lwe-engine";

int runSystemctl (const QStringList& args) {
    QProcess process;
    process.start ("systemctl", QStringList() << "--user" << args);
    if (!process.waitForStarted (3000) || !process.waitForFinished (10000))
        return -1;
    return process.exitCode ();
}

} // namespace

namespace Systemd {

QString unitPath () {
    const QString base = QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation);
    return base + "/systemd/user/" + kUnitName + ".service";
}

QString unitFileContent (const Config& config) {
    // systemd ExecStart quoting: double-quote arguments containing spaces
    QString exec;
    const QStringList argv = buildArgv (config);
    for (const QString& arg : argv) {
        if (!exec.isEmpty ())
            exec += QLatin1Char (' ');
        exec += (arg.contains (' ') || arg.contains ('"')) ? '"' + arg + '"' : arg;
    }

    return QStringLiteral (
        "[Unit]\n"
        "Description=linux-wallpaperengine dynamic wallpaper\n"
        "PartOf=graphical-session.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "Environment=DISPLAY=%1\n"
        "Environment=XAUTHORITY=%h/.Xauthority\n"
        "ExecStart=%2\n"
        "Restart=on-failure\n"
        "RestartSec=3\n"
        "\n"
        "[Install]\n"
        "WantedBy=graphical-session.target\n")
        .arg (config.display, exec);
}

bool writeUnitFile (const Config& config) {
    const QString path = unitPath ();
    QDir ().mkpath (QFileInfo (path).absolutePath ());
    QFile file (path);
    if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write (unitFileContent (config).toUtf8 ());
    return true;
}

bool daemonReload () { return runSystemctl ({ "daemon-reload" }) == 0; }

bool restartUnit () { return runSystemctl ({ "restart", kUnitName }) == 0; }

bool stopUnit () { return runSystemctl ({ "stop", kUnitName }) == 0; }

QString unitState () {
    QProcess process;
    process.start ("systemctl", QStringList() << "--user" << "is-active" << kUnitName);
    if (!process.waitForStarted (3000) || !process.waitForFinished (10000))
        return "unknown";
    return QString::fromUtf8 (process.readAllStandardOutput ()).trimmed ();
}

} // namespace Systemd
