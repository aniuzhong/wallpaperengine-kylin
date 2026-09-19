#include "cli.h"

#include "argvbuilder.h"
#include "config.h"
#include "integration.h"
#include "library.h"
#include "systemd.h"

#include <QApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <cstdio>

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAIL = 1;
constexpr int EXIT_USAGE = 2;

void printUsage () {
    std::fputs (
        "usage: wallpaper-engine <command> [options]\n"
        "\n"
        "commands:\n"
        "  start                       install the unit (if needed) and start the wallpaper\n"
        "  stop                        pause: stop the engine unit\n"
        "  restart                     restart the engine unit\n"
        "  resume                      alias of start\n"
        "  status [--json]             unit state + current wallpaper\n"
        "  list [--json]               wallpapers available in the workshop directory\n"
        "  switch <id|--random>        switch the wallpaper and persist it\n"
        "                             [--screen S] targets one screen (default: first)\n"
        "  pause / resume              alias of stop / start\n"
        "  properties <id>             list the engine properties of a wallpaper\n"
        "  setup-integration           configure peony injection for a visible desktop\n"
        "  doctor                      dump diagnostics for bug reports\n"
        "  selftest                    config load/save self-test\n",
        stdout);
}

QProcessEnvironment cliEnv () {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment ();
    env.insert ("LD_PRELOAD", QCoreApplication::applicationDirPath () + "/libpeony-alpha-shim.so");
    return env;
}

int cmdStatus (bool json) {
    const Config config = Config::load ();
    const QString state = Systemd::unitState ();
    std::fprintf (stderr, "S3 state=%s\n", state.toUtf8 ().constData ());
    if (!json) {
        std::printf ("unit: %s (%s)\n", Systemd::unitName ().toUtf8 ().constData (), state.toUtf8 ().constData ());
        for (auto it = config.screens.begin (); it != config.screens.end (); ++it)
            std::printf ("screen %s: %s\n", it.key ().toUtf8 ().constData (), it.value ().toUtf8 ().constData ());
        std::printf ("engine: %s\n", config.enginePath.toUtf8 ().constData ());
        return EXIT_OK;
    }
    QJsonObject status;
    status.insert ("unit", Systemd::unitName ());
    status.insert ("state", state);
    QJsonObject screens;
    for (auto it = config.screens.begin (); it != config.screens.end (); ++it)
        screens.insert (it.key (), it.value ());
    status.insert ("screens", screens);
    status.insert ("enginePath", config.enginePath);

    QJsonObject root;
    root.insert ("status", status);
    std::printf ("%s\n", QJsonDocument (root).toJson (QJsonDocument::Compact).constData ());
    return EXIT_OK;
}

int cmdList (bool json) {
    const Config config = Config::load ();
    const QList<WallpaperEntry> entries = scanLibrary (config.workshopDir);
    if (json) {
        QJsonArray arr;
        for (const WallpaperEntry& e : entries) {
            QJsonObject o;
            o.insert ("id", e.id);
            o.insert ("title", e.title);
            o.insert ("type", e.type);
            arr.append (o);
        }
        std::printf ("%s\n", QJsonDocument (QJsonArray { arr }).toJson (QJsonDocument::Compact).constData ());
        return EXIT_OK;
    }
    for (const WallpaperEntry& e : entries)
        std::printf ("%s  [%s]  %s\n", e.id.toUtf8 ().constData (), e.type.toUtf8 ().constData (),
                     e.title.toUtf8 ().constData ());
    return EXIT_OK;
}

int cmdSwitch (const QStringList& args) {
    QString id;
    QString screen;
    bool random = false;
    for (const QString& a : args) {
        if (a == "--random")
            random = true;
        else if (a == "--screen" || a.startsWith ("--screen="))
            continue; // multi-screen selection lands with the UI iteration
        else if (!a.startsWith ("-"))
            id = a;
    }

    const Config config = Config::load ();
    const QList<WallpaperEntry> library = scanLibrary (config.workshopDir);
    if (library.isEmpty ()) {
        std::printf ("switch: no wallpapers found in %s\n", config.workshopDir.toUtf8 ().constData ());
        return EXIT_FAIL;
    }
    if (random)
        id = library.at (QRandomGenerator::global ()->bounded (library.size ())).id;

    bool known = false;
    for (const WallpaperEntry& e : library)
        known |= (e.id == id);
    if (!known) {
        std::printf ("switch: unknown wallpaper id %s\n", id.toUtf8 ().constData ());
        return EXIT_FAIL;
    }

    Config updated = config;
    if (updated.screens.isEmpty ())
        updated.screens.insert (QGuiApplication::primaryScreen () ? QGuiApplication::primaryScreen ()->name ()
                                                                  : QString ("DP-0"),
                                id);
    else
        updated.screens.begin ().value () = id; // single-screen v1

    if (!updated.save () || !Systemd::writeUnitFile (updated) || !Systemd::daemonReload ()
        || !Systemd::restartUnit ()) {
        std::printf ("switch: failed to (re)start the engine unit\n");
        return EXIT_FAIL;
    }
    std::printf ("switched: %s\n", id.toUtf8 ().constData ());
    return EXIT_OK;
}

int cmdProperties (const QString& id) {
    const Config config = Config::load ();
    QProcess engine;
    engine.start (config.enginePath,
                  QStringList { "--list-properties", "--assets-dir", config.assetsDir, id });
    if (!engine.waitForStarted (5000) || !engine.waitForFinished (30000)) {
        engine.kill ();
        std::printf ("properties: engine did not finish in time\n");
        return EXIT_FAIL;
    }
    std::fputs (QString::fromUtf8 (engine.readAllStandardOutput ()).toUtf8 ().constData (), stdout);
    std::fputs (QString::fromUtf8 (engine.readAllStandardError ()).toUtf8 ().constData (), stderr);
    return engine.exitCode ();
}

int cmdSetupIntegration () {
    QString error;
    QApplication::setOverrideCursor (Qt::WaitCursor);
    const bool ok = Integration::setup (&error);
    QApplication::restoreOverrideCursor ();
    if (ok) {
        std::printf ("integration configured: peony injected, desktop transparent\n");
        return EXIT_OK;
    }
    std::printf ("integration failed: %s\n", error.toUtf8 ().constData ());
    return EXIT_FAIL;
}

int cmdDoctor () {
    const Config config = Config::load ();
    std::printf ("config: %s (%s)\n", Config::configPath ().toUtf8 ().constData (),
                 QFile::exists (Config::configPath ()) ? "present" : "missing");
    std::printf ("engine binary: %s (%s)\n", config.enginePath.toUtf8 ().constData (),
                 QFile::exists (config.enginePath) ? "present" : "MISSING");
    std::printf ("assets dir: %s (%s)\n", config.assetsDir.toUtf8 ().constData (),
                 QDir (config.assetsDir).exists () ? "present" : "MISSING");
    std::printf ("workshop dir: %s (%d wallpapers)\n", config.workshopDir.toUtf8 ().constData (),
                 static_cast<int> (scanLibrary (config.workshopDir).size ()));

    const qint64 pid = Integration::detect ().peonyPid;
    std::printf ("peony: pid=%lld %s\n", static_cast<long long> (pid),
                 pid > 0 ? (Integration::detect ().shimLoaded ? "injected" : "running WITHOUT shim") : "not running");

    std::printf ("unit %s: %s, unit file %s\n", Systemd::unitName ().toUtf8 ().constData (),
                 Systemd::unitState ().toUtf8 ().constData (), Systemd::unitPath ().toUtf8 ().constData ());
    return EXIT_OK;
}

} // namespace

int runCli (const QStringList& args) {
    if (args.isEmpty () || args.first () == "help" || args.first () == "--help") {
        printUsage ();
        return args.isEmpty () ? EXIT_USAGE : EXIT_OK;
    }

    const QString command = args.first ();
    const QStringList rest = args.mid (1);
    const bool json = rest.contains ("--json");

    if (command == "start" || command == "resume") {
        if (!Systemd::writeUnitFile (Config::load ()) || !Systemd::daemonReload ())
            return EXIT_FAIL;
        return Systemd::startUnit () ? EXIT_OK : EXIT_FAIL;
    }
    if (command == "stop" || command == "pause")
        return Systemd::stopUnit () ? EXIT_OK : EXIT_FAIL;
    if (command == "restart")
        return Systemd::restartUnit () ? EXIT_OK : EXIT_FAIL;
    if (command == "status")
        return cmdStatus (json);
    if (command == "list")
        return cmdList (json);
    if (command == "switch")
        return cmdSwitch (rest);
    if (command == "properties")
        return rest.isEmpty () ? EXIT_USAGE : cmdProperties (rest.first ());
    if (command == "setup-integration")
        return cmdSetupIntegration ();
    if (command == "doctor")
        return cmdDoctor ();

    std::printf ("unknown command: %s\n\n", command.toUtf8 ().constData ());
    printUsage ();
    return EXIT_USAGE;
}
