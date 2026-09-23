#include "projection.h"

#include <algorithm>

namespace projection {

lwe::Arguments ToArguments(const config::Config& config) {
    lwe::Arguments arguments;
    arguments.assetsDir = config.assetsDir;

    for (const auto& [screen, wallpaper] : config.screens) {
        lwe::ScreenBinding binding;
        binding.screen = screen;
        binding.background = wallpaper;
        binding.scaling = config.scaling;
        binding.clamp = config.clamp;
        arguments.screens.push_back(std::move(binding));
    }

    arguments.fps = config.fps;
    arguments.pauseOnFullscreen = config.fullscreenPause;
    arguments.automute = config.automute;
    arguments.audioProcessing = config.audioProcessing;
    arguments.volume = config.volume;
    arguments.silent = config.silent;
    arguments.disableParticles = config.disableParticles;
    arguments.disableMouse = config.disableMouse;
    arguments.disableParallax = config.disableParallax;

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
        for (const auto& [key, value] : props)
            arguments.properties.emplace_back(key, value);
    }

    return arguments;
}

} // namespace projection