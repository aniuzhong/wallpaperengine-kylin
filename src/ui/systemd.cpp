#include "systemd.h"

#include "argvbuilder.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace {

// overridable for tests (WALLPAPER_ENGINE_UNIT=<name>)
QString unitNameFromEnv () {
    static const QString name = qEnvironmentVariable ("WALLPAPER_ENGINE_UNIT", "wallpaper-engine");
    return name;
}

int runSystemctl (const QStringList& args) {
    QProcess process;
    process.start ("systemctl", QStringList() << "--user" << args);
    if (!process.waitForStarted (3000) || !process.waitForFinished (10000))
        return -1;
    return process.exitCode ();
}

} // namespace

namespace Systemd {

QString unitName () { return unitNameFromEnv (); }

QString unitPath () {
    // deliberately $HOME-based (not XStandardPaths): the USER systemd manager
    // must see this exact file, so XDG overrides from test shells must not
    // relocate it. Tests isolate themselves by unit name instead.
    const QString home = qEnvironmentVariable ("HOME");
    return home + "/.config/systemd/user/" + unitNameFromEnv () + ".service";
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

bool startUnit () { return runSystemctl ({ "start", unitNameFromEnv () }) == 0; }

bool restartUnit () { return runSystemctl ({ "restart", unitNameFromEnv () }) == 0; }

bool stopUnit () { return runSystemctl ({ "stop", unitNameFromEnv () }) == 0; }

bool resetFailedUnit () { return runSystemctl ({ "reset-failed", unitName () }) == 0; }

QString unitState () {
    QProcess process;
    process.start ("systemctl", QStringList() << "--user" << "is-active" << unitNameFromEnv ());
    if (!process.waitForStarted (3000) || !process.waitForFinished (10000))
        return "unknown";
    return QString::fromUtf8 (process.readAllStandardOutput ()).trimmed ();
}

} // namespace Systemd
