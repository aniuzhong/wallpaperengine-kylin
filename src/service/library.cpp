#include "library.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {
qint64 directorySize(const QString& path) {
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
} // namespace

QList<WallpaperEntry> scanLibrary(const QString& workshopDir) {
    QList<WallpaperEntry> entries;

    QDir root(workshopDir);
    if (!root.exists())
        return entries;

    QDirIterator it(workshopDir, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString dirPath = it.next();
        QFile projectFile(dirPath + "/project.json");
        if (!projectFile.open(QIODevice::ReadOnly))
            continue;

        const QJsonObject project = QJsonDocument::fromJson(projectFile.readAll()).object();
        WallpaperEntry entry;
        entry.id = QFileInfo(dirPath).fileName();
        entry.title = project.value("title").toString(entry.id);
        entry.type = project.value("type").toString("unknown");
        entry.sizeBytes = static_cast<quint64> (directorySize(dirPath));

        // the project declares its own preview file — authors ship gif, jpg
        // or png; preview.jpg is only the conventional fallback
        QString previewName = project.value("preview").toString();
        if (previewName.isEmpty())
            previewName = "preview.jpg";
        const QString previewPath = dirPath + "/" + previewName;
        if (QFile::exists (previewPath))
            entry.previewPath = previewPath;

        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(), [] (const WallpaperEntry& a, const WallpaperEntry& b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return entries;
}
