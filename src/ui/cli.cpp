#include "cli.h"

#include "argvbuilder.h"
#include "config.h"
#include "integration.h"
#include "library.h"
#include "engineunit.h"

#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
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

int cmdStatus (bool json) {
    const Config config = Config::load ();
    const QString state = EngineUnit::unitState ();
    // the unit file is what systemd actually runs; config.json is the
    // editor's draft. Prefer the unit's own ExecStart so status tells the
    // truth even after manual unit edits; fall back to config when the
    // unit file does not exist (or carries no wallpaper) yet.
    QMap<QString, QString> screens = EngineUnit::unitBackgrounds ();
    if (screens.isEmpty ())
        screens = config.screens;
    if (!json) {
        std::printf ("unit: %s (%s)\n", EngineUnit::unitName ().toUtf8 ().constData (), state.toUtf8 ().constData ());
        for (auto it = screens.begin (); it != screens.end (); ++it)
            std::printf ("screen %s: %s\n", it.key ().toUtf8 ().constData (), it.value ().toUtf8 ().constData ());
        std::printf ("engine: %s\n", config.enginePath.toUtf8 ().constData ());
        return EXIT_OK;
    }
    QJsonObject status;
    status.insert ("unit", EngineUnit::unitName ());
    status.insert ("state", state);
    QJsonObject screensJson;
    for (auto it = screens.begin (); it != screens.end (); ++it)
        screensJson.insert (it.key (), it.value ());
    status.insert ("screens", screensJson);
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
    EngineUnit::assignScreen (updated, id);

    if (!EngineUnit::applyConfig (updated)) {
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
    // no wait cursor here: the headless control plane runs under a plain
    // QCoreApplication (and a CLI has no cursor to override anyway); the GUI
    // setup path in mainwindow.cpp sets its own
    const bool ok = Integration::setup (&error);
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

    const Integration::Status peony = Integration::detect ();
    std::printf ("peony: pid=%lld %s\n", static_cast<long long> (peony.peonyPid),
                 peony.peonyPid > 0 ? (peony.shimLoaded ? "injected" : "running WITHOUT shim") : "not running");

    std::printf ("unit %s: %s, unit file %s\n", EngineUnit::unitName ().toUtf8 ().constData (),
                 EngineUnit::unitState ().toUtf8 ().constData (), EngineUnit::unitPath ().toUtf8 ().constData ());
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
        if (!EngineUnit::writeUnitFile (Config::load ()) || !EngineUnit::daemonReload ())
            return EXIT_FAIL;
        return EngineUnit::startUnit () ? EXIT_OK : EXIT_FAIL;
    }
    if (command == "stop" || command == "pause")
        return EngineUnit::stopUnit () ? EXIT_OK : EXIT_FAIL;
    if (command == "restart")
        return EngineUnit::restartUnit () ? EXIT_OK : EXIT_FAIL;
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
