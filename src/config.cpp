#include "config.h"

#include "posix.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace we {
namespace paths {

inline std::string HomeDir() {
    const char* home = getenv("HOME");
    if (home != nullptr && *home != '\0')
        return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return pw->pw_dir;
    return {};
}

inline std::string ExeDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[static_cast<size_t>(n)] = '\0';
    const std::string exe(buf);
    const size_t slash = exe.find_last_of('/');
    return slash == std::string::npos ? std::string() : exe.substr(0, slash);
}

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

inline std::string ProductDataDir() {
    return DataHome() + "/wallpaper-engine";
}

inline std::string ProductConfigDir() {
    return ConfigHome() + "/wallpaper-engine";
}

inline std::string ConfigFile() {
    return ProductConfigDir() + "/config.json";
}

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

inline std::vector<std::string> SteamRoots() {
    const std::string home = HomeDir();
    return {
        home + "/.steam/steam",
        home + "/.local/share/Steam",
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
        home + "/snap/steam/common/.local/share/Steam",
    };
}

inline std::vector<std::string> SteamAssetsCandidates() {
    static const char* suffix = "/steamapps/common/wallpaper_engine/assets";
    std::vector<std::string> candidates;
    for (const std::string& root : SteamRoots())
        candidates.push_back(root + suffix);
    return candidates;
}

inline std::vector<std::string> SteamWorkshopCandidates() {
    static const char* suffix = "/steamapps/workshop/content/431960";
    std::vector<std::string> candidates;
    for (const std::string& root : SteamRoots())
        candidates.push_back(root + suffix);
    return candidates;
}

} // namespace paths
} // namespace we

namespace config {

namespace {

std::string firstExisting(const std::vector<std::string>& candidates, const std::string& fallback) {
    for (const std::string& candidate : candidates)
        if (!candidate.empty() && fs::exists(candidate))
            return candidate;
    return fallback;
}

// json extraction that keeps the fallback on a missing key OR a wrong type —
// the QJsonDocument value(key, default) semantics this file used to rely on.
std::string getStr(const json& obj, const char* key, const std::string& dflt) {
    const auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : dflt;
}

int getInt(const json& obj, const char* key, int dflt) {
    const auto it = obj.find(key);
    return it != obj.end() && it->is_number_integer() ? it->get<int>() : dflt;
}

bool getBool(const json& obj, const char* key, bool dflt) {
    const auto it = obj.find(key);
    return it != obj.end() && it->is_boolean() ? it->get<bool>() : dflt;
}

// property values are stored as text — the engine takes --set-property
// key=value strings, and non-string json leaves are stringified the way
// QVariant::toString used to flatten them
std::string propertyToString(const json& value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}

// Bumped only by a schema change that needs migration. save() writes it;
// load() ignores it until some future loader has a migration to branch on.
constexpr int kSchemaVersion = 1;

} // namespace

Config Config::Load(we::Error* problem) {
    Config config;

    // Resolve defaults from standard install locations; the config file
    // (and a .deb install) overrides them.
    const std::vector<std::string> engineCandidates = we::paths::EngineCandidates();
    config.enginePath = firstExisting(engineCandidates, engineCandidates.front());

    // an empty result is intentional: argvbuilder then omits --assets-dir
    // and the engine runs its own auto-detection
    config.assetsDir = firstExisting(we::paths::SteamAssetsCandidates(), "");
    config.workshopDir = firstExisting(we::paths::SteamWorkshopCandidates(),
                                       we::paths::SteamWorkshopCandidates().front());

    std::error_code existsEc;
    const std::string configPath = we::paths::ConfigFile();
    std::ifstream file(configPath);
    if (!file.is_open()) {
        // a config that was never written is the normal first-run case; one
        // that exists and cannot be opened is a real failure
        if (problem != nullptr && fs::exists(configPath, existsEc)) {
            problem->kind = we::Error::FileError;
            problem->message = "cannot read " + configPath;
        }
        return config;
    }

    json obj;
    try {
        file >> obj;
    } catch (const std::exception& parseError) {
        // corrupt file: the freshly resolved defaults survive, but the
        // caller now learns why it is looking at defaults
        if (problem != nullptr) {
            problem->kind = we::Error::CorruptConfig;
            problem->message = configPath + ": " + parseError.what();
        }
        return config;
    }
    if (!obj.is_object()) {
        if (problem != nullptr) {
            problem->kind = we::Error::CorruptConfig;
            problem->message = configPath + ": not a JSON object";
        }
        return config;
    }

    config.enginePath = getStr(obj, "enginePath", config.enginePath);
    config.assetsDir = getStr(obj, "assetsDir", config.assetsDir);
    config.workshopDir = getStr(obj, "workshopDir", config.workshopDir);
    config.display = getStr(obj, "display", config.display);
    config.scaling = getStr(obj, "scaling", config.scaling);
    config.clamp = getStr(obj, "clamp", config.clamp);
    config.fps = getInt(obj, "fps", config.fps);
    config.fullscreenPause = getBool(obj, "fullscreenPause", config.fullscreenPause);
    config.automute = getBool(obj, "automute", config.automute);
    config.audioProcessing = getBool(obj, "audioProcessing", config.audioProcessing);
    config.volume = getInt(obj, "volume", config.volume);
    config.silent = getBool(obj, "silent", config.silent);
    config.disableParticles = getBool(obj, "disableParticles", config.disableParticles);
    config.disableMouse = getBool(obj, "disableMouse", config.disableMouse);
    config.disableParallax = getBool(obj, "disableParallax", config.disableParallax);

    if (auto screens = obj.find("screens"); screens != obj.end() && screens->is_object())
        for (auto screen = screens->begin(); screen != screens->end(); ++screen)
            if (screen.value().is_string())
                config.screens[screen.key()] = screen.value().get<std::string>();

    if (auto props = obj.find("properties"); props != obj.end() && props->is_object())
        for (auto wallpaper = props->begin(); wallpaper != props->end(); ++wallpaper)
            if (wallpaper.value().is_object())
                for (auto prop = wallpaper.value().begin(); prop != wallpaper.value().end(); ++prop)
                    config.properties[wallpaper.key()][prop.key()] = propertyToString(prop.value());

    return config;
}

we::Result<void> Config::Save() const {
    std::error_code ec;
    fs::create_directories(we::paths::ProductConfigDir(), ec);

    json obj;
    obj["schemaVersion"] = kSchemaVersion;
    obj["enginePath"] = enginePath;
    obj["assetsDir"] = assetsDir;
    obj["workshopDir"] = workshopDir;
    obj["scaling"] = scaling;
    obj["clamp"] = clamp;
    obj["fps"] = fps;
    obj["fullscreenPause"] = fullscreenPause;
    obj["automute"] = automute;
    obj["audioProcessing"] = audioProcessing;
    obj["volume"] = volume;
    obj["silent"] = silent;
    obj["disableParticles"] = disableParticles;
    obj["disableMouse"] = disableMouse;
    obj["disableParallax"] = disableParallax;
    obj["display"] = display;
    obj["screens"] = screens;

    json properties = json::object();
    for (const auto& [wallpaperId, props] : this->properties) {
        json entry = json::object();
        for (const auto& [key, value] : props)
            entry[key] = value;
        properties[wallpaperId] = entry;
    }
    obj["properties"] = properties;

    // indented, matching QJsonDocument::Indented; replaced atomically so a
    // concurrent reader (or a crash) never sees a half-written config
    return we::WriteFileAtomic(we::paths::ConfigFile(), obj.dump(2) + "\n");
}


Config AssignScreen(Config config, const std::string& screen, const std::string& wallpaperId) {
    // a screens[""] entry would be unmatchable by the engine: leave the
    // config untouched rather than write one
    if (screen.empty() || wallpaperId.empty())
        return config;
    config.screens[screen] = wallpaperId;
    return config;
}

std::string DefaultScreenFor(const Config& config, const std::string& primaryOutput) {
    if (config.screens.count(primaryOutput) != 0)
        return primaryOutput; // the desktop already drives it
    if (config.screens.size() == 1)
        return config.screens.begin()->first; // the only candidate there is
    return primaryOutput; // fresh config, or a multi-screen one without the primary
}

} // namespace config
