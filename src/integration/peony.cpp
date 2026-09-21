#include "backend.h"

#include "../peonybuilder.h"
#include "../posix.h"
#include "../systemdunit.h"

#include <png.h>
#include <systemd/sd-bus.h>

#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

// The per-user data dir for this backend's consumables (marker wallpaper,
// recorded previous background, shim log): the product's data dir, scoped
// per backend as the Names section in the README prescribes.
std::string dataDir() {
    // GenericDataLocation: XDG_DATA_HOME or ~/.local/share
    return wallpaper_engine::envOr("XDG_DATA_HOME", wallpaper_engine::homeDir() + "/.local/share") +
           "/wallpaper-engine/peony";
}

std::string markerPath() {
    return dataDir() + "/marker.png";
}

// What the desktop's wallpaper was before setup pointed it at the marker.
// Written by setup(), read (and removed) by teardown(); without it the only
// remaining copy is inside the environment the injected peony was launched
// with.
std::string previousBackgroundPath() {
    return dataDir() + "/previous-background";
}

// read a (small) file whole; /proc files report size 0, so drain with
// reads instead of trusting the file size
bool readSmallFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

std::vector<int64_t> findPeonyPids() {
    std::vector<int64_t> pids;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator("/proc", ec)) {
        const std::string name = entry.path().filename().string();
        // numeric directory names only
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos)
            continue;
        // only this user's desktop: another session's peony is not ours to
        // touch (a command-line match system-wide would kill it)
        struct stat st;
        if (::stat(entry.path().c_str(), &st) != 0 || st.st_uid != getuid())
            continue;
        std::string cmdline;
        if (!readSmallFile((entry.path() / "cmdline").string(), cmdline))
            continue;
        if (Peony::isPeonyDesktopCmdline(cmdline))
            pids.push_back(atoll(name.c_str()));
    }
    return pids;
}

int64_t findPeonyPid() {
    const std::vector<int64_t> pids = findPeonyPids();
    return pids.empty() ? 0 : pids.front();
}

// One variable out of the running peony's environment. This is the kernel's
// copy of what the process was exec'd with, so it still shows the launch-time
// variables even though the shim drops LD_PRELOAD from the live environment.
std::string peonyEnvValue(const std::string& name) {
    const int64_t pid = findPeonyPid();
    if (pid == 0)
        return {};
    std::string environment;
    if (!readSmallFile("/proc/" + std::to_string(pid) + "/environ", environment))
        return {};

    const std::string prefix = name + "=";
    size_t start = 0;
    while (start < environment.size()) {
        const size_t end = environment.find('\0', start);
        const size_t stop = end == std::string::npos ? environment.size() : end;
        const std::string entry = environment.substr(start, stop - start);
        if (entry.rfind(prefix, 0) == 0)
            return entry.substr(prefix.size());
        start = stop + 1;
    }
    return {};
}

bool shimMapped(int64_t pid) {
    std::string maps;
    return readSmallFile("/proc/" + std::to_string(pid) + "/maps", maps) &&
           maps.find("libpeony-alpha.so") != std::string::npos;
}

bool peonyGone() {
    return findPeonyPids().empty();
}

bool waitForPeonyExit(int timeoutMs) {
    while (timeoutMs > 0) {
        if (peonyGone())
            return true;
        wallpaper_engine::sleepMs(100);
        timeoutMs -= 100;
    }
    return peonyGone();
}

// SIGTERM the desktop, then SIGKILL whatever is left after a grace period.
void stopPeonyProcesses() {
    for (const int64_t pid : findPeonyPids())
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    if (!waitForPeonyExit(3000)) {
        for (const int64_t pid : findPeonyPids())
            ::kill(static_cast<pid_t>(pid), SIGKILL);
        wallpaper_engine::sleepMs(300);
    }
}

// peony keeps a single-instance lock in /tmp; a stale one makes the instance
// we are about to start believe it should hand over to a peony that is gone.
void clearSingleInstanceLocks() {
    std::error_code iteratorEc;
    std::error_code removeEc;
    for (const fs::directory_entry& entry : fs::directory_iterator("/tmp", iteratorEc)) {
        if (entry.path().filename().string().rfind("qtsingleapp-peonyq", 0) == 0)
            fs::remove(entry.path(), removeEc);
    }
}

// accountsservice on the system bus, addressed directly — the former
// dbus-send subprocesses with their text parsing. Read calls keep the
// former dbus-send patience (5s).

// One system-bus method call; nullptr (caller unrefs) on any failure.
sd_bus_message* callAccounts(sd_bus* bus, const char* path, const char* iface, const char* member,
                             const char* types, ...) {
    va_list ap;
    va_start(ap, types);
    sd_bus_message* m = nullptr;
    int rc = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.Accounts", path, iface, member);
    if (rc >= 0)
        rc = sd_bus_message_appendv(m, types, ap);
    va_end(ap);

    sd_bus_message* reply = nullptr;
    if (rc >= 0)
        rc = sd_bus_call(bus, m, 5 * 1000000ULL, nullptr, &reply);
    sd_bus_message_unref(m);
    if (rc < 0) {
        sd_bus_message_unref(reply);
        return nullptr;
    }
    return reply;
}

// FindUserById -> /org/freedesktop/Accounts/User<N>
std::string accountUserObjectPath() {
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0)
        return {};

    std::string path;
    sd_bus_message* reply = callAccounts(bus, "/org/freedesktop/Accounts", "org.freedesktop.Accounts",
                                         "FindUserById", "x", static_cast<int64_t>(getuid()));
    if (reply != nullptr) {
        const char* object = nullptr;
        if (sd_bus_message_read(reply, "o", &object) >= 0 && object != nullptr)
            path = object;
        sd_bus_message_unref(reply);
    }
    sd_bus_flush_close_unref(bus);
    return path;
}

// accountsservice normalizes (copies) the wallpaper into
// /var/lib/AccountsService/backgrounds — peony loads the normalized path at
// startup, so callers need it back.
std::string getAccountBackground() {
    const std::string userPath = accountUserObjectPath();
    if (userPath.empty())
        return {};
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0)
        return {};

    std::string result;
    sd_bus_message* reply = callAccounts(bus, userPath.c_str(), "org.freedesktop.DBus.Properties", "Get", "ss",
                                         "org.freedesktop.Accounts.User", "BackgroundFile");
    if (reply != nullptr) {
        const char* background = nullptr;
        if (sd_bus_message_read(reply, "v", "s", &background) >= 0 && background != nullptr)
            result = background;
        sd_bus_message_unref(reply);
    }
    sd_bus_flush_close_unref(bus);
    return result;
}

void setAccountBackground(const std::string& marker) {
    const std::string userPath = accountUserObjectPath();
    if (userPath.empty())
        return;
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0)
        return;
    sd_bus_call_method(bus, "org.freedesktop.Accounts", userPath.c_str(), "org.freedesktop.Accounts.User",
                       "SetBackgroundFile", nullptr, nullptr, "s", marker.c_str());
    sd_bus_flush_close_unref(bus);
}

// Write the marker wallpaper: 64x64 solid magenta RGB. The pixels are
// irrelevant to the design (the shim nullifies the image at load time);
// magenta only makes an unshimmed desktop obvious instead of silently dark.
// libpng is guaranteed on the target — freetype itself links it.
bool writeMarkerPng(const std::string& path) {
    constexpr int kSize = 64;

    FILE* file = fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        fclose(file);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_write_struct(&png, nullptr);
        fclose(file);
        return false;
    }

    // libpng reports errors through longjmp back into this point
    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_write_struct(&png, &info);
        fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_set_IHDR(png, info, kSize, kSize, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
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
    const bool ok = ferror(file) == 0;
    fclose(file);
    return ok;
}

// fire-and-forget tool run; blocks until exit, result ignored — the shape
// QProcess::execute() gave the gsettings call
void runTool(const std::string& program, const std::vector<std::string>& args) {
    const pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        std::vector<char*> argv{const_cast<char*>(program.c_str())};
        for (const std::string& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(program.c_str(), argv.data());
        _exit(127); // exec failed
    }
    int status = 0;
    waitpid(pid, &status, 0);
}

} // namespace

namespace Integration {

// Locate libpeony-alpha.so: probe the frontend binary's own directory
// (build tree, and layouts that ship the pair together), the library
// directory a bin/ + lib/ install() layout produces, then the standard
// system library paths. Returns an empty string when nothing matches.
std::string locateShim() {
    const std::string name = "libpeony-alpha.so";
    std::vector<std::string> candidates;
    const std::string appDir = wallpaper_engine::exeDir();
    if (!appDir.empty()) {
        candidates.push_back(appDir + "/" + name);          // build tree, sibling of the frontend
        candidates.push_back(appDir + "/../lib/" + name);   // install() layout: bin/ + lib/
        candidates.push_back(appDir + "/../lib64/" + name);
    }
    candidates.push_back("/usr/lib/" + name);
    candidates.push_back("/usr/local/lib/" + name);
    candidates.push_back("/usr/lib/x86_64-linux-gnu/" + name);
    for (const std::string& candidate : candidates)
        if (fs::exists(candidate))
            return candidate;
    return {};
}

// The peony (UKUI desktop) implementation of Backend: everything here that
// is not the generic orchestration described in integration.h is peony
// knowledge — the process name, the accountsservice/gsettings wallpaper
// pointers, the single-instance lock.
class PeonyBackend : public Backend {
public:
    Status detect() override {
        Status status;
        status.peonyPid = findPeonyPid();
        if (status.peonyPid != 0)
            status.shimLoaded = shimMapped(status.peonyPid);
        return status;
    }

    bool setup(wallpaper_engine::Error* error) override {
        // ---- 1. marker wallpaper; accountsservice and gsettings point at it.
        // The shim nullifies the pixmap at load time, so the color is
        // irrelevant (magenta makes an unshimmed desktop obvious instead of
        // silently dark).
        std::error_code fsEc;
        fs::create_directories(dataDir(), fsEc);
        fs::remove(dataDir() + "/peony-alpha.log", fsEc); // fresh log per setup

        const std::string marker = markerPath();
        if (!writeMarkerPng(marker)) {
            if (error != nullptr) {
                error->kind = wallpaper_engine::Error::FileError;
                error->message = "cannot write marker wallpaper to " + marker;
            }
            return false;
        }

        // remember the pre-change wallpaper too: if the accountsservice write
        // fails, peony still loads the OLD path and the shim must match it
        std::string previousBackground = getAccountBackground();

        // Record it — teardown() has no other way to know what to put back,
        // and getting that wrong leaves a magenta desktop and no way home.
        //
        // Recording is also where a re-run gets careful: if setup has run
        // before (no record file, but a peony is already running injected)
        // then the accountsservice value is our own marker, not the user's
        // wallpaper. The list that peony was launched with still starts with
        // the real one.
        std::error_code recordEc;
        if (!fs::exists(previousBackgroundPath(), recordEc)) {
            const std::string fromRunning = Peony::firstWallpaperIn(peonyEnvValue("PEONY_ALPHA_WALLPAPER"));
            if (!fromRunning.empty())
                previousBackground = fromRunning;
            wallpaper_engine::writeFileAtomic(previousBackgroundPath(), previousBackground + "\n");
        }

        setAccountBackground(marker);
        const std::string normalized = getAccountBackground();
        runTool("gsettings", {"set", "org.mate.background", "picture-filename", marker});

        // peony loads the accountsservice-normalized path at startup; gsettings
        // is what switchBackground() reads on wallpaper changes. Collect every
        // candidate path — the shim matches exact paths, basenames, and
        // anything under the accountsservice store anyway.
        const std::string wallpaperList = Peony::buildWallpaperList(marker, normalized, previousBackground);

        // ---- 2. stop peony and wait for a real exit; a lingering process
        // holds the single-instance lock and our injected instance would bail
        // out.
        stopPeonyProcesses();

        // ---- 3. clear stale single-instance locks and relaunch with the
        // shim preloaded (systemd-run keeps it injected across crashes).
        clearSingleInstanceLocks();

        // launch through the typed systemd layer: transient unit with
        // Restart=on-failure — if peony dies, systemd restarts it WITH the
        // injection environment (structurally guaranteed self-healing)
        SystemdLayer::SystemdUnit peonyUnit("wallpaper-engine-peony");
        // a stale failed unit with the same name blocks re-creation — clear
        // it first; both calls are no-ops when nothing is loaded
        peonyUnit.stop();
        peonyUnit.resetFailed();

        const std::string shimPath = locateShim();
        if (shimPath.empty()) {
            if (error != nullptr) {
                error->kind = wallpaper_engine::Error::FileError;
                error->message = "libpeony-alpha.so not found next to the frontend or in the standard "
                                 "library paths (looked in " +
                                 wallpaper_engine::exeDir() + ")";
            }
            return false;
        }
        const std::string logPath = dataDir() + "/peony-alpha.log";
        SystemdLayer::Error unitError;
        const std::map<std::string, std::string> peonyEnv = Peony::buildShimEnvironment(shimPath, wallpaperList);
        if (!peonyUnit.startTransient({"/usr/bin/peony-qt-desktop", "-w", "-d"}, peonyEnv, {}, &unitError)) {
            // the typed D-Bus error survives to the caller: kind and error name
            if (error != nullptr)
                *error = unitError;
            return false;
        }

        // ---- 4. verify the shim actually mapped into the new instance
        for (int waited = 0; waited < 10000; waited += 300) {
            wallpaper_engine::sleepMs(300);
            const int64_t pid = findPeonyPid();
            if (pid == 0)
                continue;
            if (shimMapped(pid)) {
                if (error != nullptr)
                    *error = {};
                return true;
            }
        }
        if (error != nullptr) {
            error->kind = wallpaper_engine::Error::Unknown;
            error->message = "peony relaunched but the shim did not map (list: " + wallpaperList +
                             "; log: " + logPath + ")";
        }
        return false;
    }

    bool teardown(wallpaper_engine::Error* error) override {
        // ---- 1. which wallpaper to put back. The recorded copy is
        // authoritative; without it (an install from before it existed) the
        // list the running peony was launched with still names it first.
        std::string previous;
        if (std::string contents; readSmallFile(previousBackgroundPath(), contents)) {
            previous = contents;
            while (!previous.empty() && (previous.back() == '\n' || previous.back() == '\r'))
                previous.pop_back();
        }
        if (previous.empty())
            previous = Peony::firstWallpaperIn(peonyEnvValue("PEONY_ALPHA_WALLPAPER"));

        // ---- 2. is the desktop actually injected? A peony that is already
        // clean is left running: restarting it would only cost the user
        // their icons.
        const int64_t existingPid = findPeonyPid();
        const bool injected = existingPid != 0 && shimMapped(existingPid);

        if (injected) {
            SystemdLayer::SystemdUnit peonyUnit("wallpaper-engine-peony");
            // stop the supervisor first: killing peony while
            // Restart=on-failure is watching would only bring it back
            peonyUnit.stop();
            peonyUnit.resetFailed();
            stopPeonyProcesses();
            clearSingleInstanceLocks();
        }

        // ---- 3. hand the desktop its own wallpaper back
        if (!previous.empty()) {
            setAccountBackground(previous);
            runTool("gsettings", {"set", "org.mate.background", "picture-filename", previous});
        }

        // leave nothing of ours behind: the record, the shim's log and the
        // marker are all consumables of an integration that no longer exists
        std::error_code removeEc;
        fs::remove(previousBackgroundPath(), removeEc);
        fs::remove(dataDir() + "/peony-alpha.log", removeEc);
        fs::remove(markerPath(), removeEc);

        // ---- 4. relaunch peony with nothing injected — an ordinary detached
        // process, so once this returns no unit of ours is supervising
        // anything
        if (injected || existingPid == 0) {
            if (!wallpaper_engine::spawnDetached({"/usr/bin/peony-qt-desktop", "-w", "-d"})) {
                if (error != nullptr) {
                    error->kind = wallpaper_engine::Error::FileError;
                    error->message = "cannot start /usr/bin/peony-qt-desktop; start it again from the "
                                     "session menu to get the desktop icons back";
                }
                return false;
            }
        }

        // ---- 5. verify: alive, and genuinely without the shim
        for (int waited = 0; waited < 10000; waited += 300) {
            wallpaper_engine::sleepMs(300);
            const int64_t pid = findPeonyPid();
            if (pid == 0)
                continue;
            if (!shimMapped(pid)) {
                if (error != nullptr)
                    *error = {};
                return true;
            }
        }
        if (error != nullptr) {
            error->kind = wallpaper_engine::Error::Unknown;
            error->message = "peony is running but the shim is still mapped into it";
        }
        return false;
    }
};

Backend& peonyBackend() {
    static PeonyBackend backend;
    return backend;
}

} // namespace Integration

// ---- facade: the three entry points cli.cpp and ui.cpp call ---------------

Integration::Status Integration::detect() {
    return peonyBackend().detect();
}

bool Integration::setup(wallpaper_engine::Error* error) {
    return peonyBackend().setup(error);
}

bool Integration::teardown(wallpaper_engine::Error* error) {
    return peonyBackend().teardown(error);
}
