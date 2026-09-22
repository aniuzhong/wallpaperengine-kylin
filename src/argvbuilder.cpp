#include "argvbuilder.h"

#include <algorithm>

namespace argvbuilder {

engine::Invocation Project(const config::Config& config) {
    engine::Invocation invocation;
    invocation.assetsDir = config.assetsDir;

    for (const auto& [screen, wallpaper] : config.screens) {
        engine::ScreenBinding binding;
        binding.screen = screen;
        binding.background = wallpaper;
        binding.scaling = config.scaling;
        binding.clamp = config.clamp;
        invocation.screens.push_back(std::move(binding));
    }

    invocation.fps = config.fps;
    invocation.fullscreenPause = config.fullscreenPause;
    invocation.automute = config.automute;
    invocation.audioProcessing = config.audioProcessing;
    invocation.volume = config.volume;
    invocation.silent = config.silent;
    invocation.disableParticles = config.disableParticles;
    invocation.disableMouse = config.disableMouse;
    invocation.disableParallax = config.disableParallax;

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
            invocation.properties.emplace_back(key, value);
    }

    return invocation;
}

std::vector<std::string> BuildArgv(const config::Config& config) {
    const std::vector<std::string> args = engine::Emit(Project(config));

    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(config.enginePath);
    argv.insert(argv.end(), args.begin(), args.end());
    return argv;
}

} // namespace argvbuilder
