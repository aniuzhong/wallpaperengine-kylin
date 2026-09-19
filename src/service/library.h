#pragma once

#include <QList>
#include <QString>

struct WallpaperEntry {
    QString id;           // workshop directory name, used as --bg value
    QString title;        // project.json title
    QString type;         // project.json type (scene/video/web)
    QString previewPath;  // preview.jpg path, empty when absent
};

// Scan a Wallpaper Engine workshop content directory. Directories without a
// project.json are skipped; entries are sorted by title.
QList<WallpaperEntry> scanLibrary (const QString& workshopDir);
