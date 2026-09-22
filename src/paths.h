#pragma once

// Every location this product computes, in one header. Pure path
// computation: no exists() checks, no I/O — the callers keep the selection
// policy (first-existing, create-directories, atomic write).
//
// The header is deliberately std-only and header-only so both the service
// layer and the shim (src/shim/peony-alpha.cpp, a zero-link interposer)
// compile it in; the shim log path and the service data dir therefore
// cannot drift apart.
//
// Two subtleties the API shape guards:
//   - SystemdUserUnit is $HOME-based on purpose: the user manager reads
//     $HOME/.config/systemd and knows nothing of XDG overrides, so a test
//     shell's XDG_CONFIG_HOME must not relocate the unit file. It is not a
//     ConfigHome() derivative and must never become one.
//   - the on-disk product name stays "wallpaper-engine" (README "Names");
//     the short `we` namespace is code-only spelling.

#include <cstdint>
#include <string>
#include <vector>

#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>

namespace we {
namespace paths {

// ---- base dirs -------------------------------------------------------------

// $HOME, falling back to the passwd entry of the effective user.
inline std::string HomeDir() {
    const char* home = getenv("HOME");
    if (home != nullptr && *home != '\0')
        return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return pw->pw_dir;
    return {};
}

// Directory of the running executable (empty when the link cannot be
// resolved) — the anchor of every install-relative candidate list below.
inline std::string ExeDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    const std::string exe(buf);
    const size_t slash = exe.find_last_of('/');
    return slash == std::string::npos ? std::string() : exe.substr(0, slash);
}

// XDG base dirs, with the spec's $HOME defaults.
inline std::string DataHome() {
    const char* dataHome = getenv("XDG_DATA_HOME");
    if (dataHome != nullptr && *dataHome != '\0')
        return dataHome;
    return HomeDir() + "/.local/share";
}

inline std::string ConfigHome() {
    const char* configHome = getenv("XDG_CONFIG_HOME");
    if (configHome != nullptr && *configHome != '\0')
        return configHome;
    return HomeDir() + "/.config";
}

// ---- product (README "Names") ----------------------------------------------

inline std::string ProductDataDir() {
    return DataHome() + "/wallpaper-engine";
}

inline std::string ProductConfigDir() {
    return ConfigHome() + "/wallpaper-engine";
}

inline std::string ConfigFile() {
    return ProductConfigDir() + "/config.json";
}

// ---- peony backend consumables ----------------------------------------------

inline std::string PeonyDataDir() {
    return ProductDataDir() + "/peony";
}

// the fallback wallpaper the shim nullifies at load time
inline std::string PeonyMarker() {
    return PeonyDataDir() + "/marker.png";
}

// where Setup records the desktop's pre-integration wallpaper for Teardown
inline std::string PeonyPreviousBackground() {
    return PeonyDataDir() + "/previous-background";
}

// the shim's always-on log — defined here so the shim and the service layer
// share one definition of it
inline std::string PeonyShimLog() {
    return PeonyDataDir() + "/peony-alpha.log";
}

// ---- systemd ----------------------------------------------------------------

// The user manager reads $HOME/.config/systemd — $HOME deliberately, not
// ConfigHome(); see the header comment.
inline std::string SystemdUserUnit(const std::string& unit) {
    return HomeDir() + "/.config/systemd/user/" + unit + ".service";
}

// ---- install and content layouts (ordered candidate lists) ------------------

// libpeony-alpha.so: the frontend binary's own directory (build tree, and
// layouts that ship the pair together), the lib/ directories an install()
// layout produces, then the standard system library paths.
inline std::vector<std::string> ShimCandidates() {
    std::vector<std::string> candidates;
    const std::string appDir = ExeDir();
    if (!appDir.empty()) {
        candidates.push_back(appDir + "/libpeony-alpha.so");
        candidates.push_back(appDir + "/../lib/libpeony-alpha.so");
        candidates.push_back(appDir + "/../lib64/libpeony-alpha.so");
    }
    candidates.push_back("/usr/lib/libpeony-alpha.so");
    candidates.push_back("/usr/local/lib/libpeony-alpha.so");
    candidates.push_back("/usr/lib/x86_64-linux-gnu/libpeony-alpha.so");
    return candidates;
}

// the engine binary: deb payload layout, standard bin dirs, then the two
// install-relative layouts (bin/ sibling of the flat engine install, flat
// dev tree)
inline std::vector<std::string> EngineCandidates() {
    std::vector<std::string> candidates {
        "/opt/wallpaper-engine/linux-wallpaperengine",
        "/usr/local/bin/linux-wallpaperengine",
        "/usr/bin/linux-wallpaperengine",
    };
    const std::string appDir = ExeDir();
    if (!appDir.empty()) {
        candidates.push_back(appDir + "/../linux-wallpaperengine");
        candidates.push_back(appDir + "/linux-wallpaperengine");
    }
    return candidates;
}

// Steam install layouts, matching linux-wallpaperengine's own
// auto-detection list (native, ~/.steam symlink, flatpak, snap).
inline std::vector<std::string> SteamRoots() {
    const std::string home = HomeDir();
    return {
        home + "/.steam/steam",
        home + "/.local/share/Steam",
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
        home + "/snap/steam/common/.local/share/Steam",
    };
}

// the engine's asset directory under every Steam root (empty entries are
// the caller's "run the engine's own auto-detection" signal)
inline std::vector<std::string> SteamAssetsCandidates() {
    static const char* suffix = "/steamapps/common/wallpaper_engine/assets";
    std::vector<std::string> candidates;
    for (const std::string& root : SteamRoots())
        candidates.push_back(root + suffix);
    return candidates;
}

// the workshop content directory of the Wallpaper Engine app (431960)
inline std::vector<std::string> SteamWorkshopCandidates() {
    static const char* suffix = "/steamapps/workshop/content/431960";
    std::vector<std::string> candidates;
    for (const std::string& root : SteamRoots())
        candidates.push_back(root + suffix);
    return candidates;
}

} // namespace paths
} // namespace we
