#include "integration.h"

#include "peonybuilder.h"
#include "posix.h"
#include "systemdunit.h"

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

std::string dataDir() {
    // GenericDataLocation: XDG_DATA_HOME or ~/.local/share
    return lwe::envOr("XDG_DATA_HOME", lwe::homeDir() + "/.local/share") + "/lwe-dynamic-wallpaper";
}

std::string markerPath() {
    return dataDir() + "/lwe-alpha-wallpaper.png";
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
        if (Integration::isPeonyDesktopCmdline(cmdline))
            pids.push_back(atoll(name.c_str()));
    }
    return pids;
}

int64_t findPeonyPid() {
    const std::vector<int64_t> pids = findPeonyPids();
    return pids.empty() ? 0 : pids.front();
}

bool shimMapped(int64_t pid) {
    std::string maps;
    return readSmallFile("/proc/" + std::to_string(pid) + "/maps", maps) &&
           maps.find("peony-alpha-shim") != std::string::npos;
}

bool peonyGone() {
    return findPeonyPids().empty();
}

bool waitForPeonyExit(int timeoutMs) {
    while (timeoutMs > 0) {
        if (peonyGone())
            return true;
        lwe::sleepMs(100);
        timeoutMs -= 100;
    }
    return peonyGone();
}

// accountsservice on the system bus, addressed directly — the former
// dbus-send subprocesses with their text parsing. Read calls keep the
// former dbus-send patience (5s).

// One system-bus method call; nullptr (caller unrefs) on any failure.
sd_bus_message* callAccounts (sd_bus* bus, const char* path, const char* iface, const char* member,
                              const char* types, ...) {
    va_list ap;
    va_start (ap, types);
    sd_bus_message* m = nullptr;
    int rc = sd_bus_message_new_method_call (bus, &m, "org.freedesktop.Accounts", path, iface, member);
    if (rc >= 0)
        rc = sd_bus_message_appendv (m, types, ap);
    va_end (ap);

    sd_bus_message* reply = nullptr;
    if (rc >= 0)
        rc = sd_bus_call (bus, m, 5 * 1000000ULL, nullptr, &reply);
    sd_bus_message_unref (m);
    if (rc < 0) {
        sd_bus_message_unref (reply);
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
    sd_bus_message* reply = callAccounts(bus, userPath.c_str(), "org.freedesktop.DBus.Properties",
                                         "Get", "ss", "org.freedesktop.Accounts.User", "BackgroundFile");
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
    sd_bus_call_method(bus, "org.freedesktop.Accounts", userPath.c_str(),
                       "org.freedesktop.Accounts.User", "SetBackgroundFile", nullptr, nullptr,
                       "s", marker.c_str());
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
        std::vector<char*> argv { const_cast<char*> (program.c_str ()) };
        for (const std::string& arg : args)
            argv.push_back (const_cast<char*> (arg.c_str ()));
        argv.push_back (nullptr);
        execvp(program.c_str(), argv.data());
        _exit(127); // exec failed
    }
    int status = 0;
    waitpid(pid, &status, 0);
}

} // namespace

namespace Integration {

// Locate libpeony-alpha-shim.so: probe the frontend binary's own directory
// (build tree, and layouts that ship the pair together), the library
// directory a bin/ + lib/ install() layout produces, then the standard
// system library paths. Returns an empty string when nothing matches.
std::string locateShim() {
    const std::string name = "libpeony-alpha-shim.so";
    std::vector<std::string> candidates;
    const std::string appDir = lwe::exeDir();
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

Status detect() {
    Status status;
    status.peonyPid = findPeonyPid();
    if (status.peonyPid != 0)
        status.shimLoaded = shimMapped(status.peonyPid);
    return status;
}

bool setup(lwe::Error* error) {
    // ---- 1. marker wallpaper; accountsservice and gsettings point at it.
    // The shim nullifies the pixmap at load time, so the color is irrelevant
    // (magenta makes an unshimmed desktop obvious instead of silently dark).
    std::error_code fsEc;
    fs::create_directories(dataDir(), fsEc);
    fs::remove(dataDir() + "/peony-shim.log", fsEc); // fresh log per setup

    const std::string marker = markerPath();
    if (!writeMarkerPng(marker)) {
        if (error != nullptr) {
            error->kind = lwe::Error::FileError;
            error->message = "cannot write marker wallpaper to " + marker;
        }
        return false;
    }

    // remember the pre-change wallpaper too: if the accountsservice write
    // fails, peony still loads the OLD path and the shim must match it
    const std::string previousBackground = getAccountBackground();

    setAccountBackground(marker);
    const std::string normalized = getAccountBackground();
    runTool("gsettings", { "set", "org.mate.background", "picture-filename", marker });

    // peony loads the accountsservice-normalized path at startup; gsettings
    // is what switchBackground() reads on wallpaper changes. Collect every
    // candidate path — the shim matches exact paths, basenames, and anything
    // under the accountsservice store anyway.
    const std::string wallpaperList = buildWallpaperList(marker, normalized, previousBackground);

    // ---- 2. stop peony and wait for a real exit; a lingering process holds
    // the single-instance lock and our injected instance would bail out.
    for (const int64_t pid : findPeonyPids())
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    if (!waitForPeonyExit(3000)) {
        for (const int64_t pid : findPeonyPids())
            ::kill(static_cast<pid_t>(pid), SIGKILL);
        lwe::sleepMs(300);
    }

    // ---- 3. clear stale single-instance locks and relaunch with the shim
    // preloaded (systemd-run keeps it injected across crashes).
    std::error_code iterEc;
    for (const fs::directory_entry& entry : fs::directory_iterator("/tmp", iterEc)) {
        const std::string lock = entry.path().filename().string();
        if (lock.rfind("qtsingleapp-peonyq", 0) == 0)
            fs::remove(entry.path(), fsEc);
    }

    // launch through the typed systemd layer: transient unit with
    // Restart=on-failure — if peony dies, systemd restarts it WITH the
    // injection environment (structurally guaranteed self-healing)
    SystemdLayer::SystemdUnit peonyUnit("linux-wallpaperengine-peony");
    // a stale failed unit with the same name blocks re-creation — clear it
    // first; both calls are no-ops when nothing is loaded
    peonyUnit.stop();
    peonyUnit.resetFailed();

    const std::string shimPath = locateShim();
    if (shimPath.empty()) {
        if (error != nullptr) {
            error->kind = lwe::Error::FileError;
            error->message = "libpeony-alpha-shim.so not found next to the frontend or in the standard "
                             "library paths (looked in " + lwe::exeDir() + ")";
        }
        return false;
    }
    const std::string logPath = dataDir() + "/peony-shim.log";
    SystemdLayer::Error unitError;
    const std::map<std::string, std::string> peonyEnv = buildShimEnvironment(shimPath, wallpaperList, logPath);
    if (!peonyUnit.startTransient({ "/usr/bin/peony-qt-desktop", "-w", "-d" }, peonyEnv, {}, &unitError)) {
        // the typed D-Bus error survives to the caller: kind and error name
        if (error != nullptr)
            *error = unitError;
        return false;
    }

    // ---- 4. verify the shim actually mapped into the new instance
    for (int waited = 0; waited < 10000; waited += 300) {
        lwe::sleepMs(300);
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
        error->kind = lwe::Error::Unknown;
        error->message = "peony relaunched but the shim did not map (list: " + wallpaperList +
                         "; log: " + logPath + ")";
    }
    return false;
}

} // namespace Integration
