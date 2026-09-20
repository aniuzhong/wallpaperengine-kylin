#include "cli.h"

#include "argvbuilder.h"
#include "config.h"
#include "engineunit.h"
#include "integration.h"
#include "library.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <filesystem>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

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
    nlohmann::json screensJson = nlohmann::json::object ();
    for (const auto& [screen, wallpaper] : screens)
        screensJson[screen] = wallpaper;
    nlohmann::json status;
    status["unit"] = EngineUnit::unitName ();
    status["state"] = state;
    status["screens"] = std::move (screensJson);
    status["enginePath"] = config.enginePath;

    nlohmann::json root;
    root["status"] = std::move (status);
    std::printf ("%s\n", root.dump ().c_str ());
    return EXIT_OK;
}

int cmdList (bool json) {
    const Config config = Config::load ();
    const std::vector<WallpaperEntry> entries = scanLibrary (config.workshopDir);
    if (json) {
        nlohmann::json arr = nlohmann::json::array ();
        for (const WallpaperEntry& e : entries) {
            nlohmann::json o;
            o["id"] = e.id;
            o["title"] = e.title;
            o["type"] = e.type;
            arr.push_back (std::move (o));
        }
        // consumers parse the wrapped shape; do not unwrap
        std::printf ("%s\n", nlohmann::json::array ({ std::move (arr) }).dump ().c_str ());
        return EXIT_OK;
    }
    for (const WallpaperEntry& e : entries)
        std::printf ("%s  [%s]  %s\n", e.id.c_str (), e.type.c_str (), e.title.c_str ());
    return EXIT_OK;
}

int cmdSwitch (const std::vector<std::string>& args) {
    std::string id;
    bool random = false;
    for (const std::string& a : args) {
        if (a == "--random")
            random = true;
        else if (a == "--screen" || a.rfind ("--screen=", 0) == 0)
            continue; // multi-screen selection lands with the UI iteration
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

    Config updated = config;
    EngineUnit::assignScreen (updated, id);

    if (!EngineUnit::applyConfig (updated)) {
        std::printf ("switch: failed to (re)start the engine unit\n");
        return EXIT_FAIL;
    }
    std::printf ("switched: %s\n", id.c_str ());
    return EXIT_OK;
}

// Run the engine as a child with stdout/stderr streamed to ours, bounded by
// |timeoutMs| (SIGKILL past the deadline). Returns the child exit code, or
// -1 when the child could not be spawned or was killed (|=timedOut| tells
// which); 127 means the engine path itself was not executable.
int runEngineCaptured (const std::string& enginePath, const std::vector<std::string>& args,
                       long timeoutMs, bool* timedOut) {
    *timedOut = false;
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (pipe (outPipe) != 0)
        return -1;
    if (pipe (errPipe) != 0) {
        close (outPipe[0]);
        close (outPipe[1]);
        return -1;
    }

    const pid_t pid = fork ();
    if (pid < 0) {
        for (int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1]})
            close (fd);
        return -1;
    }
    if (pid == 0) {
        dup2 (outPipe[1], STDOUT_FILENO);
        dup2 (errPipe[1], STDERR_FILENO);
        for (int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1]})
            close (fd);
        std::vector<char*> childArgs;
        childArgs.push_back (const_cast<char*> (enginePath.c_str ()));
        for (const std::string& arg : args)
            childArgs.push_back (const_cast<char*> (arg.c_str ()));
        childArgs.push_back (nullptr);
        execvp (enginePath.c_str (), childArgs.data ());
        _exit (127);
    }

    close (outPipe[1]);
    close (errPipe[1]);
    const long long deadline = [] {
        timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        return static_cast<long long> (now.tv_sec) * 1000 + now.tv_nsec / 1000000;
    } () + timeoutMs;

    int readers[2] = {outPipe[0], errPipe[0]};
    int openReaders = 2;
    char buffer[4096];
    while (openReaders > 0) {
        timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        const long long remaining = deadline - (static_cast<long long> (now.tv_sec) * 1000 + now.tv_nsec / 1000000);
        if (remaining <= 0) {
            *timedOut = true;
            break;
        }
        pollfd fds[2] = {{readers[0], POLLIN, 0}, {readers[1], POLLIN, 0}};
        const int ready = poll (fds, 2, static_cast<int> (remaining));
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        for (int i = 0; i < 2; i++) {
            if ((fds[i].revents & (POLLIN | POLLHUP)) == 0)
                continue;
            const ssize_t n = read (readers[i], buffer, sizeof (buffer));
            if (n <= 0) {
                close (readers[i]);
                readers[i] = -1;
                openReaders--;
                continue;
            }
            std::fwrite (buffer, 1, static_cast<size_t> (n), i == 0 ? stdout : stderr);
        }
    }
    for (int fd : readers)
        if (fd != -1)
            close (fd);

    if (*timedOut)
        kill (pid, SIGKILL);
    int status = 0;
    waitpid (pid, &status, 0);
    if (*timedOut)
        return -1;
    return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
}

int cmdProperties (const std::string& id) {
    const Config config = Config::load ();
    bool timedOut = false;
    const int exitCode = runEngineCaptured (
        config.enginePath, {"--list-properties", "--assets-dir", config.assetsDir, id}, 30000, &timedOut);
    if (timedOut || exitCode == -1 || exitCode == 127) {
        std::printf ("properties: engine did not finish in time\n");
        return EXIT_FAIL;
    }
    return exitCode;
}

int cmdSetupIntegration () {
    std::string error;
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
    std::error_code ec;
    const std::string configPath = Config::configPath ();
    std::printf ("config: %s (%s)\n", configPath.c_str (),
                 fs::exists (configPath, ec) ? "present" : "missing");
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
    return config.save () ? EXIT_OK : EXIT_FAIL;
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
        return rest.empty () ? EXIT_USAGE : cmdProperties (rest.front ());
    if (command == "setup-integration")
        return cmdSetupIntegration ();
    if (command == "doctor")
        return cmdDoctor ();
    if (command == "selftest" || command == "--selftest")
        return cmdSelftest ();

    std::printf ("unknown command: %s\n\n", command.c_str ());
    printUsage ();
    return EXIT_USAGE;
}
