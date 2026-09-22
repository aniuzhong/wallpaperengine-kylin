#include "argvbuilder.h"

#include <algorithm>

namespace argvbuilder {

std::vector<std::string> BuildArgv(const config::Config& config) {
    std::vector<std::string> argv { config.enginePath };

    if (!config.assetsDir.empty()) {
        argv.push_back("--assets-dir");
        argv.push_back(config.assetsDir);
    }

    for (const auto& [screen, wallpaper] : config.screens) {
        argv.push_back("--screen-root");
        argv.push_back(screen);
        argv.push_back("--bg");
        argv.push_back(wallpaper);
        if (!config.scaling.empty()) {
            argv.push_back("--scaling");
            argv.push_back(config.scaling);
        }
        if (!config.clamp.empty()) {
            argv.push_back("--clamp");
            argv.push_back(config.clamp);
        }
    }

    argv.push_back("--fps");
    argv.push_back(std::to_string(config.fps));
    if (!config.fullscreenPause)
        argv.push_back("--no-fullscreen-pause");
    if (!config.automute)
        argv.push_back("--noautomute");
    if (!config.audioProcessing)
        argv.push_back("--no-audio-processing");

    if (config.silent)
        argv.push_back("--silent");
    else {
        argv.push_back("--volume");
        argv.push_back(std::to_string(config.volume));
    }

    if (config.disableParticles)
        argv.push_back("--disable-particles");
    if (config.disableMouse)
        argv.push_back("--disable-mouse");
    if (config.disableParallax)
        argv.push_back("--disable-parallax");

    // only pass properties belonging to a wallpaper that is actually being
    // launched: shared property names (schemecolor, ...) must not leak from
    // one wallpaper into another
    std::vector<std::string> activeIds;
    for (const auto& [screen, wallpaper] : config.screens)
        if (std::find(activeIds.begin(), activeIds.end(), wallpaper) == activeIds.end())
            activeIds.push_back(wallpaper);

    for (const auto& [wallpaperId, props] : config.properties) {
        if (std::find(activeIds.begin(), activeIds.end(), wallpaperId) == activeIds.end())
            continue;
        for (const auto& [key, value] : props) {
            argv.push_back("--set-property");
            argv.push_back(key + "=" + value);
        }
    }

    return argv;
}

} // namespace argvbuilder
