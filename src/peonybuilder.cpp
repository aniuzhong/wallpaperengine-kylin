#include "peonybuilder.h"

#include <algorithm>
#include <vector>

namespace Peony {

bool isPeonyDesktopCmdline(const std::string& cmdline) {
    return cmdline.find("peony-qt-desktop") != std::string::npos;
}

std::string buildWallpaperList(const std::string& marker, const std::string& normalized,
                               const std::string& previous) {
    std::vector<std::string> paths;
    for (const std::string& candidate : { previous, normalized, marker }) {
        if (candidate.empty())
            continue;
        if (std::find(paths.begin(), paths.end(), candidate) != paths.end())
            continue;
        paths.push_back(candidate);
    }

    std::string joined;
    for (const std::string& path : paths) {
        if (!joined.empty())
            joined += ':';
        joined += path;
    }
    return joined;
}

std::string firstWallpaperIn(const std::string& list) {
    const size_t colon = list.find(':');
    return colon == std::string::npos ? list : list.substr(0, colon);
}

std::map<std::string, std::string> buildShimEnvironment(const std::string& shimPath,
                                                        const std::string& wallpaperList,
                                                        const std::string& logPath) {
    return {
        { "LD_PRELOAD", shimPath },
        { "PEONY_ALPHA_WALLPAPER", wallpaperList },
        { "PEONY_ALPHA_LOG", logPath },
    };
}

} // namespace Peony
