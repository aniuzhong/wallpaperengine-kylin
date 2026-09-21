#pragma once

#include "error.h"

#include <cstdint>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

// The config file is a JSON projection of linux-wallpaperengine's CLI
// arguments. Every field maps to an engine flag.
//
// The file carries a "schemaVersion" key so a future schema change can
// recognize and migrate files written by an older build. The loader
// deliberately does not read it today: it is tolerant of unknown keys and
// wrongly-typed values, and when a version finally needs different
// treatment, branching on the key is the migration hook.
struct Config {
    std::string enginePath;                                       // engine binary
    std::string assetsDir;                                        // --assets-dir
    std::string workshopDir;                                      // wallpaper library scan path (not an engine flag)
    std::map<std::string, std::string> screens;                   // screen -> wallpaper ID (--screen-root/--bg)
    std::string display = ":0";                                   // DISPLAY for the engine systemd unit
    std::string scaling = "fill";                                 // --scaling
    std::string clamp = "border";                                 // --clamp
    int fps = 30;                                                 // --fps
    bool fullscreenPause = true;                                  // false emits --no-fullscreen-pause
    bool automute = true;                                         // false emits --noautomute
    bool audioProcessing = true;                                  // false emits --no-audio-processing
    int volume = 15;                                              // --volume
    bool silent = true;                                           // --silent
    bool disableParticles = false;                                // --disable-particles
    bool disableMouse = false;                                    // --disable-mouse
    bool disableParallax = false;                                 // --disable-parallax
    std::map<std::string, std::map<std::string, std::string>> properties; // wallpaper ID -> {property: value} (--set-property)

    static std::string configDir();  // ~/.config/wallpaper-engine
    static std::string configPath();

    // A missing file is not an error: the resolved defaults are the answer.
    // A file that exists but does not parse reports CorruptConfig — the
    // defaults still come back, but the caller can now tell the two apart
    // (and say so, instead of silently showing defaults).
    static Config load(wallpaper_engine::Error* error = nullptr);

    // Atomic replace (write-temp + rename): a concurrent reader sees the old
    // config or the new one, never a truncated file.
    bool save(wallpaper_engine::Error* error = nullptr) const;

    // A copy with the user-editable fields updated from |patch|. Unknown keys
    // are ignored (forward compatibility) and so are wrongly-typed values
    // (the current value survives) — the same tolerance load() shows the
    // file. Screens and per-wallpaper properties are not patchable: they are
    // the result of applying a wallpaper, not a setting.
    Config patched(const nlohmann::json& patch) const;
};
