#pragma once

#include "error.h"
#include "result.h"

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

    static std::string ConfigDir();  // ~/.config/wallpaper-engine
    static std::string ConfigPath();

    // A missing file is not an error: the resolved defaults are the answer.
    // A file that exists but does not parse reports CorruptConfig — the
    // defaults still come back, but the caller can now tell the two apart
    // (and say so, instead of silently showing defaults). Load never fails,
    // so the slot is a diagnostics channel, not the Result convention.
    static Config Load(wallpaper_engine::Error* problem = nullptr);

    // Atomic replace (write-temp + rename): a concurrent reader sees the old
    // config or the new one, never a truncated file.
    wallpaper_engine::Result<void> Save() const;
};

// Pure: point |screen| at |wallpaperId| and return the updated config. An
// empty screen or wallpaper leaves the config untouched (a screens[""]
// entry would be unmatchable by the engine).
Config AssignScreen(Config config, const std::string& screen, const std::string& wallpaperId);

// Pure: the screen a bare `switch` targets, given the desktop's primary
// output. Preference order — the primary output when the config already
// drives it, then the only configured screen, then the primary output. The
// rule this replaces ("whichever entry the map happened to yield first")
// followed std::map's ordering, not the desktop.
std::string DefaultScreenFor(const Config& config, const std::string& primaryOutput);
