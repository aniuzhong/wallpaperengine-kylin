#include "library.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

uint64_t directorySize(const std::string& path) {
    uint64_t total = 0;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(path, ec)) {
        std::error_code fileEc;
        if (entry.is_regular_file(fileEc))
            total += static_cast<uint64_t> (entry.file_size(fileEc));
    }
    return total;
}

// case-insensitive lexicographic order — the Qt::CaseInsensitive comparison
// the library was sorted with
bool titleLess(const WallpaperEntry& a, const WallpaperEntry& b) {
    return std::lexicographical_compare (
        a.title.begin (), a.title.end (), b.title.begin (), b.title.end (),
        [] (unsigned char x, unsigned char y) { return std::tolower (x) < std::tolower (y); });
}

} // namespace

std::vector<WallpaperEntry> scanLibrary(const std::string& workshopDir) {
    std::vector<WallpaperEntry> entries;

    std::error_code ec;
    if (!fs::is_directory(workshopDir, ec))
        return entries;

    for (const fs::directory_entry& dir : fs::directory_iterator(workshopDir, ec)) {
        if (!dir.is_directory())
            continue;
        const std::string dirPath = dir.path().string();

        std::ifstream projectFile(dirPath + "/project.json");
        if (!projectFile.is_open())
            continue;

        json project;
        try {
            projectFile >> project;
        } catch (...) {
            continue; // unreadable project.json: the directory is skipped
        }
        if (!project.is_object())
            continue;

        WallpaperEntry entry;
        entry.id = dir.path().filename().string();
        if (auto title = project.find("title"); title != project.end() && title->is_string())
            entry.title = title->get<std::string>();
        else
            entry.title = entry.id;
        if (auto type = project.find("type"); type != project.end() && type->is_string())
            entry.type = type->get<std::string>();
        else
            entry.type = "unknown";
        entry.sizeBytes = directorySize(dirPath);

        // the project declares its own preview file — authors ship gif, jpg
        // or png; preview.jpg is only the conventional fallback
        std::string previewName = "preview.jpg";
        if (auto preview = project.find("preview"); preview != project.end() && preview->is_string())
            previewName = preview->get<std::string>();
        const std::string previewPath = dirPath + "/" + previewName;
        if (fs::exists(previewPath))
            entry.previewPath = previewPath;

        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), titleLess);
    return entries;
}
