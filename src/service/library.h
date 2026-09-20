#pragma once

#include <QList>
#include <QString>

struct WallpaperEntry {
    QString id;          // workshop directory name, used as --bg value
    QString title;       // project.json title
    QString type;        // project.json type (scene/video/web)
    quint64 sizeBytes;   // on-disk size of the wallpaper directory

    // resolved preview file (project.json "preview" field, preview.jpg as
    // the conventional fallback); empty when the wallpaper ships none. The
    // frontends decode it — at their own display size, this is metadata only.
    QString previewPath;
};

// Scan a Wallpaper Engine workshop content directory. Directories without a
// project.json are skipped; entries are sorted by title. Decoding and any
// other presentation work belongs to the frontends: they get paths and
// numbers, never images.
QList<WallpaperEntry> scanLibrary(const QString& workshopDir);
