#pragma once

#include <cstdint>
#include <map>
#include <string>

// The config file is a JSON projection of linux-wallpaperengine's CLI
// arguments. Every field maps to an engine flag.
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

    static std::string configDir();  // ~/.config/lwe-dynamic-wallpaper
    static std::string configPath();
    static Config load();
    bool save() const;
};
