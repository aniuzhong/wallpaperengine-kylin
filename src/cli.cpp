#include "cli.h"

#include "argvbuilder.h"
#include "config.h"
#include "paths.h"
#include "engine_unit.h"
#include "integration.h"
#include "library.h"
#include "process.h"
#include "report.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <filesystem>

namespace fs = std::filesystem;

namespace cli {

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAIL = 1;
constexpr int EXIT_USAGE = 2;

void PrintUsage() {
    std::fputs(
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
        "  teardown-integration        undo it: restore the wallpaper, unload the shim\n"
        "  doctor                      dump diagnostics for bug reports\n"
        "  selftest                    config load/save self-test\n",
        stdout);
}

// The one failure exit: --json consumers get the structured projection,
// humans get "<what>: <why>". Every command funnels through here so a
// reason is never dropped on the floor.
int Fail(bool json, const std::string& what, const we::Error& error) {
    if (json)
        std::printf("%s\n", report::Error(error).dump().c_str());
    else
        std::printf("%s: %s\n", what.c_str(), we::Describe(error).c_str());
    return EXIT_FAIL;
}

// The Result shape of the same exit: success passes straight through as
// EXIT_OK, a failure funnels into the projection above.
int Fail(bool json, const std::string& what, we::Result<void>&& result) {
    return result ? EXIT_OK : Fail(json, what, std::move(result).error());
}

int CmdStatus(bool json) {
    const config::Config config = config::Config::Load();
    auto state = engine_unit::State();
    const std::string stateName = state ? *state : "unknown";
    // the unit file is what systemd actually runs; config.json is the
    // editor's draft. Prefer the unit's own ExecStart so status tells the
    // truth even after manual unit edits; fall back to config when the
    // unit file does not exist (or carries no wallpaper) yet.
    std::map<std::string, std::string> screens = engine_unit::UnitBackgrounds();
    if (screens.empty())
        screens = config.screens;

    if (json) {
        const nlohmann::json root =
            report::Status(engine_unit::UnitName(), stateName, screens, config.enginePath);
        std::printf("%s\n", root.dump().c_str());
        return EXIT_OK;
    }

    std::printf("unit: %s (%s)\n", engine_unit::UnitName().c_str(), stateName.c_str());
    for (const auto& [screen, wallpaper] : screens)
        std::printf("screen %s: %s\n", screen.c_str(), wallpaper.c_str());
    std::printf("engine: %s\n", config.enginePath.c_str());
    if (!state)
        std::printf("bus: %s\n", we::Describe(state.error()).c_str());
    return EXIT_OK;
}

int CmdList(bool json) {
    const config::Config config = config::Config::Load();
    const std::vector<library::WallpaperEntry> entries = library::ScanLibrary(config.workshopDir);
    if (json) {
        std::printf("%s\n", report::Library(entries).dump().c_str());
        return EXIT_OK;
    }
    for (const library::WallpaperEntry& e : entries)
        std::printf("%s  [%s]  %s\n", e.id.c_str(), e.type.c_str(), e.title.c_str());
    return EXIT_OK;
}

int CmdSwitch(const std::vector<std::string>& args, bool json) {
    constexpr const char* kScreenFlag = "--screen=";

    std::string id;
    std::string requestedScreen;
    bool random = false;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        if (a == "--random")
            random = true;
        else if (a == "--screen" && i + 1 < args.size())
            requestedScreen = args[++i];
        else if (a.rfind(kScreenFlag, 0) == 0)
            requestedScreen = a.substr(std::strlen(kScreenFlag));
        else if (!a.empty() && a[0] != '-')
            id = a;
    }

    const config::Config config = config::Config::Load();
    const std::vector<library::WallpaperEntry> library = library::ScanLibrary(config.workshopDir);
    if (library.empty()) {
        std::printf("switch: no wallpapers found in %s\n", config.workshopDir.c_str());
        return EXIT_FAIL;
    }
    if (random) {
        std::mt19937 generator(std::random_device {} ());
        std::uniform_int_distribution<size_t> pick(0, library.size() - 1);
        id = library[pick(generator)].id;
    }

    bool known = false;
    for (const library::WallpaperEntry& e : library)
        known |= (e.id == id);
    if (!known) {
        std::printf("switch: unknown wallpaper id %s\n", id.c_str());
        return EXIT_FAIL;
    }

    // The primary output is resolved here, at the boundary: the selection
    // and the config edit below are pure functions of it. Asking RandR is
    // only necessary when the caller did not name a screen.
    const std::string screen =
        requestedScreen.empty()
            ? config::DefaultScreenFor(config, engine_unit::FallbackScreenName())
            : requestedScreen;
    if (screen.empty()) {
        std::printf("switch: no screen to target — pass --screen <name>\n");
        return EXIT_FAIL;
    }

    const config::Config updated = config::AssignScreen(config, screen, id);
    if (auto applied = engine_unit::ApplyConfig(updated); !applied)
        return Fail(json, "switch", std::move(applied).error());

    std::printf("switched: %s\n", id.c_str());
    return EXIT_OK;
}

int CmdProperties(const std::string& id) {
    const config::Config config = config::Config::Load();
    std::string output;
    bool timedOut = false;
    const int exitCode = process::RunCaptured(
        config.enginePath, {"--list-properties", "--assets-dir", config.assetsDir, id},
        std::chrono::seconds(30), &output, &timedOut);
    // the engine's own listing, verbatim: buffered rather than streamed so a
    // frontend can publish it as one value
    if (!output.empty())
        std::fwrite(output.data(), 1, output.size(), stdout);

    if (process::DidNotRun(exitCode)) {
        we::Error error;
        error.kind = we::Error::Unknown;
        error.message = timedOut ? "the engine did not finish in time"
                                 : "the engine at " + config.enginePath + " could not be run";
        return Fail(false, "properties", error);
    }
    return exitCode;
}

int CmdSetupIntegration(bool json) {
    if (auto r = integration::Setup(); !r)
        return Fail(json, "integration failed", std::move(r).error());
    std::printf("integration configured: peony injected, desktop transparent\n");
    return EXIT_OK;
}

int CmdRemoveIntegration(bool json) {
    if (auto r = integration::Teardown(); !r)
        return Fail(json, "teardown-integration failed", std::move(r).error());
    std::printf("integration removed: the previous wallpaper is back, "
                 "peony is running without the shim\n");
    return EXIT_OK;
}

int CmdDoctor() {
    we::Error configError;
    const config::Config config = config::Config::Load(&configError);
    std::error_code ec;
    const std::string configPath = we::paths::ConfigFile();
    std::printf("config: %s (%s)\n", configPath.c_str(),
                 fs::exists(configPath, ec) ? "present" : "missing");
    // "present" and "readable" are different answers: say which one it is
    if (configError.kind != we::Error::NoError)
        std::printf("config problem: %s\n", we::Describe(configError).c_str());
    std::printf("engine binary: %s (%s)\n", config.enginePath.c_str(),
                 fs::exists(config.enginePath, ec) ? "present" : "MISSING");
    std::printf("assets dir: %s (%s)\n", config.assetsDir.c_str(),
                 fs::is_directory(config.assetsDir, ec) ? "present" : "MISSING");
    std::printf("workshop dir: %s (%d wallpapers)\n", config.workshopDir.c_str(),
                 static_cast<int> (library::ScanLibrary(config.workshopDir).size()));

    // three states, compared: what the config wants (desired), what the
    // installed unit file declares, and what the manager runs (the state
    // line below). A drift is not an error — it is the answer to "why does
    // the desktop not match what I configured?"
    const std::map<std::string, std::string> declared = engine_unit::UnitBackgrounds();
    int drifts = 0;
    for (const auto& [screen, wallpaper] : config.screens) {
        const auto it = declared.find(screen);
        if (it == declared.end()) {
            std::printf("drift: %s desired -> %s, not declared\n", screen.c_str(), wallpaper.c_str());
            drifts++;
        } else if (it->second != wallpaper) {
            std::printf("drift: %s desired -> %s, declared %s\n", screen.c_str(), wallpaper.c_str(),
                         it->second.c_str());
            drifts++;
        }
    }
    for (const auto& [screen, wallpaper] : declared) {
        if (config.screens.count(screen) == 0) {
            std::printf("drift: %s declared -> %s, not in config\n", screen.c_str(), wallpaper.c_str());
            drifts++;
        }
    }
    if (drifts == 0)
        std::printf("drift: desired and declared agree\n");

    const integration::Status peony = integration::Detect();
    std::printf("peony: pid=%lld %s\n", static_cast<long long> (peony.peonyPid),
                 peony.peonyPid > 0 ? (peony.shimLoaded ? "injected" : "running WITHOUT shim") : "not running");

    const std::string shimPath = integration::LocateShim();
    std::printf("shim: %s\n", shimPath.empty() ? "libpeony-alpha.so (MISSING)"
                                                    : shimPath.c_str());

    auto state = engine_unit::State();
    std::printf("unit %s: %s, unit file %s\n", engine_unit::UnitName().c_str(),
                 state.value_or("unknown").c_str(), engine_unit::UnitPath().c_str());
    return EXIT_OK;
}

int CmdSelftest() {
    // load the config (creating defaults on first run) and report
    const config::Config config = config::Config::Load();
    std::printf("config path: %s\n", we::paths::ConfigFile().c_str());
    std::printf("engine: %s\n", config.enginePath.c_str());
    std::printf("screens: %d, fps: %d, silent: %s\n", static_cast<int> (config.screens.size()), config.fps,
                 config.silent ? "true" : "false");
    if (auto saved = config.Save(); !saved)
        return Fail(false, "selftest", std::move(saved).error());
    return EXIT_OK;
}

} // namespace

int RunCli(const std::vector<std::string>& args) {
    if (args.empty() || args.front() == "help" || args.front() == "--help") {
        PrintUsage();
        return args.empty() ? EXIT_USAGE : EXIT_OK;
    }

    const std::string command = args.front();
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    const bool json = std::find(rest.begin(), rest.end(), "--json") != rest.end();

    if (command == "start" || command == "resume") {
        if (auto installed = engine_unit::WriteUnitFile(config::Config::Load()); !installed)
            return Fail(json, command, std::move(installed).error());
        if (auto reloaded = engine_unit::DaemonReload(); !reloaded)
            return Fail(json, command, std::move(reloaded).error());
        return Fail(json, command, engine_unit::StartUnit());
    }
    if (command == "stop" || command == "pause")
        return Fail(json, command, engine_unit::StopUnit());
    if (command == "restart")
        return Fail(json, command, engine_unit::RestartUnit());
    if (command == "status")
        return CmdStatus(json);
    if (command == "list")
        return CmdList(json);
    if (command == "switch")
        return CmdSwitch(rest, json);
    if (command == "properties")
        return rest.empty() ? EXIT_USAGE : CmdProperties(rest.front());
    if (command == "setup-integration")
        return CmdSetupIntegration(json);
    if (command == "teardown-integration" || command == "remove-integration")
        return CmdRemoveIntegration(json);
    if (command == "doctor")
        return CmdDoctor();
    if (command == "selftest" || command == "--selftest")
        return CmdSelftest();

    std::printf("unknown command: %s\n\n", command.c_str());
    PrintUsage();
    return EXIT_USAGE;
}

} // namespace cli
