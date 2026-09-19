#pragma once

#include <QImage>
#include <QList>
#include <QString>

// Display size of the preview image decoded for each entry: 16:9, one image
// serves the grid tile (downscaled) and the detail panel (native size).
constexpr int kPreviewW = 360;
constexpr int kPreviewH = kPreviewW * 9 / 16;

struct WallpaperEntry {
    QString id;     // workshop directory name, used as --bg value
    QString title;  // project.json title
    QString type;   // project.json type (scene/video/web)
    QString size;   // humanized on-disk size of the wallpaper directory
    QImage preview; // preview decoded at display resolution (16:9); null when absent

    // animated previews (multi-frame gif etc.): raw preview bytes for the
    // detail panel's player; empty for static previews
    QByteArray previewAnim;
    bool previewAnimated = false;
};

// Scan a Wallpaper Engine workshop content directory. Directories without a
// project.json are skipped; entries are sorted by title. Previews are
// decoded here — the ui consumes images, never paths or directory layout.
QList<WallpaperEntry> scanLibrary (const QString& workshopDir);
