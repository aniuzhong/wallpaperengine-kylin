#include "report.h"

#include <algorithm>
#include <cctype>

namespace report {

namespace {

// Whether a preview is something to play rather than to show. The extension
// is all a frontend needs to pick <video> over <img> — decoding is still
// entirely its business.
bool isVideoPreview(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == "mp4" || extension == "webm" || extension == "m4v" || extension == "mov";
}

// Wire name for an error kind. Callers branch on these strings, so they are
// part of the contract: rename one and you rename it for consumers too.
const char* kindName(wallpaper_engine::Error::Kind kind) {
    switch (kind) {
    case wallpaper_engine::Error::NoError:
        return "ok";
    case wallpaper_engine::Error::BusUnreachable:
        return "bus-unreachable";
    case wallpaper_engine::Error::NoSuchUnit:
        return "no-such-unit";
    case wallpaper_engine::Error::JobFailed:
        return "job-failed";
    case wallpaper_engine::Error::InvalidInput:
        return "invalid-input";
    case wallpaper_engine::Error::FileError:
        return "file-error";
    case wallpaper_engine::Error::CorruptConfig:
        return "corrupt-config";
    case wallpaper_engine::Error::Unknown:
        break;
    }
    return "unknown";
}

} // namespace

nlohmann::json Status(const std::string& UnitName, const std::string& state,
                      const std::map<std::string, std::string>& screens, const std::string& enginePath) {
    nlohmann::json screensJson = nlohmann::json::object();
    for (const auto& [screen, wallpaper] : screens)
        screensJson[screen] = wallpaper;

    nlohmann::json status;
    status["unit"] = UnitName;
    status["state"] = state;
    status["screens"] = std::move(screensJson);
    status["enginePath"] = enginePath;

    nlohmann::json root;
    root["status"] = std::move(status);
    return root;
}

nlohmann::json Library(const std::vector<WallpaperEntry>& entries) {
    nlohmann::json arr = nlohmann::json::array();
    for (const WallpaperEntry& entry : entries) {
        nlohmann::json o;
        o["id"] = entry.id;
        o["title"] = entry.title;
        o["type"] = entry.type;
        // size and preview availability are what a detail panel shows;
        // previewPath itself is deliberately not published — a frontend asks
        // the API for /api/preview/<id> instead of inventing file paths
        o["sizeBytes"] = entry.sizeBytes;
        o["hasPreview"] = !entry.previewPath.empty();
        if (!entry.previewPath.empty())
            o["previewKind"] = isVideoPreview(entry.previewPath) ? "video" : "image";
        arr.push_back(std::move(o));
    }
    // consumers parse the wrapped shape; do not unwrap
    return nlohmann::json::array({ std::move(arr) });
}

nlohmann::json Error(const wallpaper_engine::Error& error) {
    nlohmann::json detail;
    detail["kind"] = kindName(error.kind);
    detail["message"] = error.message.empty() ? Describe(error) : error.message;
    if (!error.dbusName.empty())
        detail["dbusName"] = error.dbusName;

    nlohmann::json root;
    root["error"] = std::move(detail);
    return root;
}

} // namespace report
