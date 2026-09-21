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
    // the conventional fallback); empty when the wallpaper ships none, or
    // when the declared name resolves outside the wallpaper's own directory
    // (the field is untrusted content, so escaping paths and symlinks are
    // rejected rather than handed to a caller). Canonical, not the
    // constructed path. The CLI decodes it — at its own display size,
    // this is metadata only.
    std::string previewPath;
};

// Scan a Wallpaper Engine workshop content directory. Directories without a
// project.json are skipped; entries are sorted by title. Decoding and any
// other presentation work belongs to the CLI: it gets paths and
// numbers, never images.
std::vector<WallpaperEntry> ScanLibrary(const std::string& workshopDir);
