#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WallpaperEntry {
    std::string id;        // workshop directory name, used as --bg value
    std::string title;     // project.json title
    std::string type;      // project.json type (scene/video/web)
    uint64_t sizeBytes;    // on-disk size of the wallpaper directory

    // resolved preview file (project.json "preview" field, preview.jpg as
    // the conventional fallback); empty when the wallpaper ships none. The
    // frontends decode it — at their own display size, this is metadata only.
    std::string previewPath;
};

// Scan a Wallpaper Engine workshop content directory. Directories without a
// project.json are skipped; entries are sorted by title. Decoding and any
// other presentation work belongs to the frontends: they get paths and
// numbers, never images.
std::vector<WallpaperEntry> scanLibrary(const std::string& workshopDir);
