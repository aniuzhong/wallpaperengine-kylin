#include "library.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {
constexpr int kPreviewW = 360;
constexpr int kPreviewH = 202; // 16:9 — one image serves grid tile and detail panel

QString humanizeSize (qint64 bytes) {
    if (bytes >= (1LL << 30))
        return QString::number (bytes / double (1LL << 30), 'f', 1) + " GB";
    if (bytes >= (1LL << 20))
        return QString::number (bytes / double (1LL << 20), 'f', 1) + " MB";
    if (bytes >= (1LL << 10))
        return QString::number (bytes / double (1LL << 10), 'f', 1) + " KB";
    return QString::number (bytes) + " B";
}

qint64 directorySize (const QString& path) {
    qint64 total = 0;
    QDirIterator it (path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext ()) {
        it.next ();
        total += it.fileInfo ().size ();
    }
    return total;
}
} // namespace

QList<WallpaperEntry> scanLibrary (const QString& workshopDir) {
    QList<WallpaperEntry> entries;

    QDir root (workshopDir);
    if (!root.exists ())
        return entries;

    QDirIterator it (workshopDir, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext ()) {
        const QString dirPath = it.next ();
        QFile projectFile (dirPath + "/project.json");
        if (!projectFile.open (QIODevice::ReadOnly))
            continue;

        const QJsonObject project = QJsonDocument::fromJson (projectFile.readAll ()).object ();
        WallpaperEntry entry;
        entry.id = QFileInfo (dirPath).fileName ();
        entry.title = project.value ("title").toString (entry.id);
        entry.type = project.value ("type").toString ("unknown");
        entry.size = humanizeSize (directorySize (dirPath));

        // decode at display size and center-crop to exactly 16:9, so grid
        // tile and detail panel distort nothing regardless of the source
        // aspect ratio
        QImageReader reader (dirPath + "/preview.jpg");
        const QSize target (kPreviewW, kPreviewH);
        reader.setScaledSize (target.scaled (target.width (), target.height (), Qt::KeepAspectRatioByExpanding));
        QImage preview = reader.read ();
        if (!preview.isNull ())
            preview = preview.copy ((preview.width () - target.width ()) / 2,
                                    (preview.height () - target.height ()) / 2,
                                    target.width (), target.height ());
        entry.preview = preview;

        entries.append (entry);
    }

    std::sort (entries.begin (), entries.end (), [] (const WallpaperEntry& a, const WallpaperEntry& b) {
        return a.title.compare (b.title, Qt::CaseInsensitive) < 0;
    });
    return entries;
}
