#include "cli.h"

#include "argvbuilder.h"
#include "config.h"
#include "engineprocess.h"
#include "engineunit.h"
#include "integration.h"
#include "library.h"
#include "report.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <filesystem>

namespace fs = std::filesystem;

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
        "                             [--screen S] targets one screen\n"
        "                             (default: the primary screen)\n"
        "  pause / resume              alias of stop / start\n"
        "  properties <id>             list the engine properties of a wallpaper\n"
        "  setup-integration           configure peony injection for a visible desktop\n"
        "  remove-integration          undo it: restore the wallpaper, unload the shim\n"
        "  doctor                      dump diagnostics for bug reports\n"
        "  selftest                    config load/save self-test\n"
        "  ui [--port N] [--no-open]   serve the browser frontend on 127.0.0.1\n",
        stdout);
}

// The one failure exit: --json consumers get the structured projection,
// humans get "<what>: <why>". Every command funnels through here so a
// reason is never dropped on the floor.
int fail (bool json, const std::string& what, const lwe::Error& error) {
    if (json)
        std::printf ("%s\n", Report::error (error).dump ().c_str ());
    else
        std::printf ("%s: %s\n", what.c_str (), lwe::describe (error).c_str ());
    return EXIT_FAIL;
}

int cmdStatus (bool json) {
    const Config config = Config::load ();
    lwe::Error busError;
    const std::string state = EngineUnit::unitState (&busError);
    // the unit file is what systemd actually runs; config.json is the
    // editor's draft. Prefer the unit's own ExecStart so status tells the
    // truth even after manual unit edits; fall back to config when the
    // unit file does not exist (or carries no wallpaper) yet.
    std::map<std::string, std::string> screens = EngineUnit::unitBackgrounds ();
    if (screens.empty ())
        screens = config.screens;

    if (json) {
        const nlohmann::json root =
            Report::status (EngineUnit::unitName (), state, screens, config.enginePath);
        std::printf ("%s\n", root.dump ().c_str ());
        return EXIT_OK;
    }

    std::printf ("unit: %s (%s)\n", EngineUnit::unitName ().c_str (), state.c_str ());
    for (const auto& [screen, wallpaper] : screens)
        std::printf ("screen %s: %s\n", screen.c_str (), wallpaper.c_str ());
    std::printf ("engine: %s\n", config.enginePath.c_str ());
    if (!busError.ok ())
        std::printf ("bus: %s\n", lwe::describe (busError).c_str ());
    return EXIT_OK;
}

int cmdList (bool json) {
    const Config config = Config::load ();
    const std::vector<WallpaperEntry> entries = scanLibrary (config.workshopDir);
    if (json) {
        std::printf ("%s\n", Report::library (entries).dump ().c_str ());
        return EXIT_OK;
    }
    for (const WallpaperEntry& e : entries)
        std::printf ("%s  [%s]  %s\n", e.id.c_str (), e.type.c_str (), e.title.c_str ());
    return EXIT_OK;
}

int cmdSwitch (const std::vector<std::string>& args, bool json) {
    constexpr const char* kScreenFlag = "--screen=";

    std::string id;
    std::string requestedScreen;
    bool random = false;
    for (size_t i = 0; i < args.size (); i++) {
        const std::string& a = args[i];
        if (a == "--random")
            random = true;
        else if (a == "--screen" && i + 1 < args.size ())
            requestedScreen = args[++i];
        else if (a.rfind (kScreenFlag, 0) == 0)
            requestedScreen = a.substr (std::strlen (kScreenFlag));
        else if (!a.empty () && a[0] != '-')
            id = a;
    }

    const Config config = Config::load ();
    const std::vector<WallpaperEntry> library = scanLibrary (config.workshopDir);
    if (library.empty ()) {
        std::printf ("switch: no wallpapers found in %s\n", config.workshopDir.c_str ());
        return EXIT_FAIL;
    }
    if (random) {
        std::mt19937 generator (std::random_device {} ());
        std::uniform_int_distribution<size_t> pick (0, library.size () - 1);
        id = library[pick (generator)].id;
    }

    bool known = false;
    for (const WallpaperEntry& e : library)
        known |= (e.id == id);
    if (!known) {
        std::printf ("switch: unknown wallpaper id %s\n", id.c_str ());
        return EXIT_FAIL;
    }

    // The primary output is resolved here, at the boundary: the selection
    // and the config edit below are pure functions of it. Asking RandR is
    // only necessary when the caller did not name a screen.
    const std::string screen =
        requestedScreen.empty ()
            ? EngineUnit::defaultScreenFor (config, EngineUnit::fallbackScreenName ())
            : requestedScreen;
    if (screen.empty ()) {
        std::printf ("switch: no screen to target — pass --screen <name>\n");
        return EXIT_FAIL;
    }

    lwe::Error error;
    const Config updated = EngineUnit::assignScreen (config, screen, id);
    if (!EngineUnit::applyConfig (updated, &error))
        return fail (json, "switch", error);

    std::printf ("switched: %s\n", id.c_str ());
    return EXIT_OK;
}

int cmdProperties (const std::string& id) {
    const Config config = Config::load ();
    std::string output;
    bool timedOut = false;
    const int exitCode = EngineProcess::runCaptured (
        config.enginePath, {"--list-properties", "--assets-dir", config.assetsDir, id}, 30000, &output,
        &timedOut);
    // the engine's own listing, verbatim: buffered rather than streamed so a
    // frontend can publish it as one value
    if (!output.empty ())
        std::fwrite (output.data (), 1, output.size (), stdout);

    if (EngineProcess::didNotRun (exitCode)) {
        lwe::Error error;
        error.kind = lwe::Error::Unknown;
        error.message = timedOut ? "the engine did not finish in time"
                                 : "the engine at " + config.enginePath + " could not be run";
        return fail (false, "properties", error);
    }
    return exitCode;
}

int cmdSetupIntegration (bool json) {
    lwe::Error error;
    if (!Integration::setup (&error))
        return fail (json, "integration failed", error);
    std::printf ("integration configured: peony injected, desktop transparent\n");
    return EXIT_OK;
}

int cmdRemoveIntegration (bool json) {
    lwe::Error error;
    if (!Integration::teardown (&error))
        return fail (json, "remove-integration failed", error);
    std::printf ("integration removed: the previous wallpaper is back, "
                 "peony is running without the shim\n");
    return EXIT_OK;
}

int cmdDoctor () {
    lwe::Error configError;
    const Config config = Config::load (&configError);
    std::error_code ec;
    const std::string configPath = Config::configPath ();
    std::printf ("config: %s (%s)\n", configPath.c_str (),
                 fs::exists (configPath, ec) ? "present" : "missing");
    // "present" and "readable" are different answers: say which one it is
    if (!configError.ok ())
        std::printf ("config problem: %s\n", lwe::describe (configError).c_str ());
    std::printf ("engine binary: %s (%s)\n", config.enginePath.c_str (),
                 fs::exists (config.enginePath, ec) ? "present" : "MISSING");
    std::printf ("assets dir: %s (%s)\n", config.assetsDir.c_str (),
                 fs::is_directory (config.assetsDir, ec) ? "present" : "MISSING");
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

int cmdSelftest () {
    // load the config (creating defaults on first run) and report
    const Config config = Config::load ();
    std::printf ("config path: %s\n", Config::configPath ().c_str ());
    std::printf ("engine: %s\n", config.enginePath.c_str ());
    std::printf ("screens: %d, fps: %d, silent: %s\n", static_cast<int> (config.screens.size ()), config.fps,
                 config.silent ? "true" : "false");
    lwe::Error error;
    if (!config.save (&error))
        return fail (false, "selftest", error);
    return EXIT_OK;
}

} // namespace

int runCli (const std::vector<std::string>& args) {
    if (args.empty () || args.front () == "help" || args.front () == "--help") {
        printUsage ();
        return args.empty () ? EXIT_USAGE : EXIT_OK;
    }

    const std::string command = args.front ();
    const std::vector<std::string> rest (args.begin () + 1, args.end ());
    const bool json = std::find (rest.begin (), rest.end (), "--json") != rest.end ();

    if (command == "start" || command == "resume") {
        lwe::Error error;
        if (!EngineUnit::writeUnitFile (Config::load (), &error) || !EngineUnit::daemonReload (&error) ||
            !EngineUnit::startUnit (&error))
            return fail (json, command, error);
        return EXIT_OK;
    }
    if (command == "stop" || command == "pause") {
        lwe::Error error;
        return EngineUnit::stopUnit (&error) ? EXIT_OK : fail (json, command, error);
    }
    if (command == "restart") {
        lwe::Error error;
        return EngineUnit::restartUnit (&error) ? EXIT_OK : fail (json, command, error);
    }
    if (command == "status")
        return cmdStatus (json);
    if (command == "list")
        return cmdList (json);
    if (command == "switch")
        return cmdSwitch (rest, json);
    if (command == "properties")
        return rest.empty () ? EXIT_USAGE : cmdProperties (rest.front ());
    if (command == "setup-integration")
        return cmdSetupIntegration (json);
    if (command == "remove-integration" || command == "teardown-integration")
        return cmdRemoveIntegration (json);
    if (command == "doctor")
        return cmdDoctor ();
    if (command == "selftest" || command == "--selftest")
        return cmdSelftest ();

    std::printf ("unknown command: %s\n\n", command.c_str ());
    printUsage ();
    return EXIT_USAGE;
}
