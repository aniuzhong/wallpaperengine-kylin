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
#include <algorithm>
#include <cstdio>
#include <map>

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
    const std::string state = EngineUnit::unitState ();
    // the unit file is what systemd actually runs; config.json is the
    // editor's draft. Prefer the unit's own ExecStart so status tells the
    // truth even after manual unit edits; fall back to config when the
    // unit file does not exist (or carries no wallpaper) yet.
    std::map<std::string, std::string> screens = EngineUnit::unitBackgrounds ();
    if (screens.empty ())
        screens = config.screens;
    if (!json) {
        std::printf ("unit: %s (%s)\n", EngineUnit::unitName ().c_str (), state.c_str ());
        for (const auto& [screen, wallpaper] : screens)
            std::printf ("screen %s: %s\n", screen.c_str (), wallpaper.c_str ());
        std::printf ("engine: %s\n", config.enginePath.c_str ());
        return EXIT_OK;
    }
    QJsonObject status;
    status.insert ("unit", QString::fromStdString (EngineUnit::unitName ()));
    status.insert ("state", QString::fromStdString (state));
    QJsonObject screensJson;
    for (const auto& [screen, wallpaper] : screens)
        screensJson.insert (QString::fromStdString (screen), QString::fromStdString (wallpaper));
    status.insert ("screens", screensJson);
    status.insert ("enginePath", QString::fromStdString (config.enginePath));

    QJsonObject root;
    root.insert ("status", status);
    std::printf ("%s\n", QJsonDocument (root).toJson (QJsonDocument::Compact).constData ());
    return EXIT_OK;
}

int cmdList (bool json) {
    const Config config = Config::load ();
    const std::vector<WallpaperEntry> entries = scanLibrary (config.workshopDir);
    if (json) {
        QJsonArray arr;
        for (const WallpaperEntry& e : entries) {
            QJsonObject o;
            o.insert ("id", QString::fromStdString (e.id));
            o.insert ("title", QString::fromStdString (e.title));
            o.insert ("type", QString::fromStdString (e.type));
            arr.append (o);
        }
        std::printf ("%s\n", QJsonDocument (QJsonArray { arr }).toJson (QJsonDocument::Compact).constData ());
        return EXIT_OK;
    }
    for (const WallpaperEntry& e : entries)
        std::printf ("%s  [%s]  %s\n", e.id.c_str (), e.type.c_str (), e.title.c_str ());
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
    const std::vector<WallpaperEntry> library = scanLibrary (config.workshopDir);
    if (library.empty ()) {
        std::printf ("switch: no wallpapers found in %s\n", config.workshopDir.c_str ());
        return EXIT_FAIL;
    }
    if (random)
        id = QString::fromStdString (
            library.at (QRandomGenerator::global ()->bounded (static_cast<int> (library.size ()))).id);

    const std::string wanted = id.toStdString ();
    bool known = false;
    for (const WallpaperEntry& e : library)
        known |= (e.id == wanted);
    if (!known) {
        std::printf ("switch: unknown wallpaper id %s\n", id.toUtf8 ().constData ());
        return EXIT_FAIL;
    }

    Config updated = config;
    EngineUnit::assignScreen (updated, wanted);

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
    engine.start (QString::fromStdString (config.enginePath),
                  QStringList { "--list-properties", "--assets-dir", QString::fromStdString (config.assetsDir), id });
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
    std::string error;
    // no wait cursor here: the headless control plane runs without a GUI
    // application (and a CLI has no cursor to override anyway); the GUI
    // setup path sets its own
    const bool ok = Integration::setup (&error);
    if (ok) {
        std::printf ("integration configured: peony injected, desktop transparent\n");
        return EXIT_OK;
    }
    std::printf ("integration failed: %s\n", error.c_str ());
    return EXIT_FAIL;
}

int cmdDoctor () {
    const Config config = Config::load ();
    const QString configPath = QString::fromStdString (Config::configPath ());
    std::printf ("config: %s (%s)\n", configPath.toUtf8 ().constData (),
                 QFile::exists (configPath) ? "present" : "missing");
    const QString enginePath = QString::fromStdString (config.enginePath);
    std::printf ("engine binary: %s (%s)\n", enginePath.toUtf8 ().constData (),
                 QFile::exists (enginePath) ? "present" : "MISSING");
    const QString assetsDir = QString::fromStdString (config.assetsDir);
    std::printf ("assets dir: %s (%s)\n", assetsDir.toUtf8 ().constData (),
                 QDir (assetsDir).exists () ? "present" : "MISSING");
    std::printf ("workshop dir: %s (%d wallpapers)\n", config.workshopDir.c_str (),
                 static_cast<int> (scanLibrary (config.workshopDir).size ()));

    const Integration::Status peony = Integration::detect ();
    std::printf ("peony: pid=%lld %s\n", static_cast<long long> (peony.peonyPid),
                 peony.peonyPid > 0 ? (peony.shimLoaded ? "injected" : "running WITHOUT shim") : "not running");

    const std::string shimPath = Integration::locateShim ();
    std::printf ("shim: %s\n", shimPath.empty () ? "libpeony-alpha-shim.so (MISSING)"
                                                    : shimPath.c_str ());

    std::printf ("unit %s: %s, unit file %s\n", EngineUnit::unitName ().c_str (),
                 EngineUnit::unitState ().c_str (), EngineUnit::unitPath ().c_str ());
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
