#include "library.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace library {

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

// Both arguments must already be canonical, which is what makes a plain
// prefix test a containment test: no ".." component survives
// canonicalization, so nothing inside |directory| can name a path outside it.
bool isInside(const std::string& directory, const std::string& path) {
    if (path.size() <= directory.size())
        return false;
    if (path.compare(0, directory.size(), directory) != 0)
        return false;
    return directory.back() == '/' || path[directory.size()] == '/';
}

// case-insensitive lexicographic order — the Qt::CaseInsensitive comparison
// the library was sorted with
bool titleLess(const WallpaperEntry& a, const WallpaperEntry& b) {
    return std::lexicographical_compare(
        a.title.begin(), a.title.end(), b.title.begin(), b.title.end(),
        [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
}

} // namespace

std::vector<WallpaperEntry> ScanLibrary(const std::string& workshopDir) {
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
        // or png; preview.jpg is only the conventional fallback.
        //
        // The declared name is untrusted input: workshop content is
        // downloaded from Steam, so a "preview" of "../../../etc/passwd" — or
        // a symlink pointing out of the directory — must not become a path a
        // frontend will happily serve. Only a regular file that resolves
        // inside the wallpaper's own directory is a preview.
        std::string previewName = "preview.jpg";
        if (auto preview = project.find("preview"); preview != project.end() && preview->is_string())
            previewName = preview->get<std::string>();

        std::error_code dirEc;
        std::error_code previewEc;
        const std::string dirCanonical = fs::weakly_canonical(dir.path(), dirEc).string();
        const std::string previewCanonical = fs::weakly_canonical(dirPath + "/" + previewName, previewEc).string();
        if (!dirEc && !previewEc && isInside(dirCanonical, previewCanonical) &&
            fs::is_regular_file(previewCanonical, previewEc))
            entry.previewPath = previewCanonical;

        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), titleLess);
    return entries;
}

} // namespace library
