#include "config.h"

#include "posix.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string firstExisting(const std::vector<std::string>& candidates, const std::string& fallback) {
    for (const std::string& candidate : candidates)
        if (!candidate.empty() && fs::exists(candidate))
            return candidate;
    return fallback;
}

// Steam install layouts, matching linux-wallpaperengine's own auto-detection
// list (native, ~/.steam symlink, flatpak, snap).
std::vector<std::string> steamRoots() {
    const std::string home = lwe::homeDir();
    return {
        home + "/.steam/steam",
        home + "/.local/share/Steam",
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
        home + "/snap/steam/common/.local/share/Steam",
    };
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

} // namespace

std::string Config::configDir() {
    return lwe::envOr("XDG_CONFIG_HOME", lwe::homeDir() + "/.config") + "/lwe-dynamic-wallpaper";
}

std::string Config::configPath() {
    return configDir() + "/config.json";
}

Config Config::load() {
    Config config;

    // Resolve defaults from standard install locations; the config file
    // (and a .deb install) overrides them.
    std::vector<std::string> engineCandidates {
        "/opt/linux-wallpaperengine/linux-wallpaperengine", // deb payload layout
        "/usr/local/bin/linux-wallpaperengine",
        "/usr/bin/linux-wallpaperengine",
    };
    const std::string appDir = lwe::exeDir();
    if (!appDir.empty()) {
        engineCandidates.push_back(appDir + "/../linux-wallpaperengine"); // deb: bin/ sibling of the flat engine install
        engineCandidates.push_back(appDir + "/linux-wallpaperengine");    // flat dev tree
    }
    config.enginePath = firstExisting(engineCandidates, "/opt/linux-wallpaperengine/linux-wallpaperengine");

    // an empty result is intentional: argvbuilder then omits --assets-dir
    // and the engine runs its own auto-detection
    std::vector<std::string> assetsCandidates;
    std::vector<std::string> workshopCandidates;
    for (const std::string& root : steamRoots()) {
        assetsCandidates.push_back(root + "/steamapps/common/wallpaper_engine/assets");
        workshopCandidates.push_back(root + "/steamapps/workshop/content/431960");
    }
    config.assetsDir = firstExisting(assetsCandidates, "");
    config.workshopDir = firstExisting(workshopCandidates, workshopCandidates.front());

    std::ifstream file(configPath());
    if (!file.is_open())
        return config;

    json obj;
    try {
        file >> obj;
    } catch (...) {
        return config; // corrupt file: the freshly resolved defaults survive
    }
    if (!obj.is_object())
        return config;

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

bool Config::save() const {
    std::error_code ec;
    fs::create_directories(configDir(), ec);

    json obj;
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

    std::ofstream file(configPath(), std::ios::trunc);
    if (!file.is_open())
        return false;
    file << obj.dump(2) << "\n"; // indented, matching QJsonDocument::Indented
    return file.good();
}
