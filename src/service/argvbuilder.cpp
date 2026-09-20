#include "argvbuilder.h"

QStringList buildArgv(const Config& config) {
    QStringList argv;
    argv << config.enginePath;

    if (!config.assetsDir.isEmpty())
        argv << "--assets-dir" << config.assetsDir;

    for (auto it = config.screens.begin(); it != config.screens.end(); ++it) {
        argv << "--screen-root" << it.key() << "--bg" << it.value();
        if (!config.scaling.isEmpty())
            argv << "--scaling" << config.scaling;
        if (!config.clamp.isEmpty())
            argv << "--clamp" << config.clamp;
    }

    argv << "--fps" << QString::number(config.fps);
    if (!config.fullscreenPause)
        argv << "--no-fullscreen-pause";
    if (!config.automute)
        argv << "--noautomute";
    if (!config.audioProcessing)
        argv << "--no-audio-processing";

    if (config.silent)
        argv << "--silent";
    else
        argv << "--volume" << QString::number(config.volume);

    if (config.disableParticles)
        argv << "--disable-particles";
    if (config.disableMouse)
        argv << "--disable-mouse";
    if (config.disableParallax)
        argv << "--disable-parallax";

    // only pass properties belonging to a wallpaper that is actually being
    // launched: shared property names (schemecolor, ...) must not leak from
    // one wallpaper into another
    QStringList activeIds;
    for (auto screenIt = config.screens.begin(); screenIt != config.screens.end(); ++screenIt)
        if (!activeIds.contains(screenIt.value()))
            activeIds << screenIt.value();

    for (auto wallIt = config.properties.begin(); wallIt != config.properties.end(); ++wallIt) {
        if (!activeIds.contains(wallIt.key()))
            continue;
        const QVariantMap props = wallIt.value().toMap();
        for (auto propIt = props.begin(); propIt != props.end(); ++propIt)
            argv << "--set-property" << propIt.key() + "=" + propIt.value().toString();
    }

    return argv;
}
