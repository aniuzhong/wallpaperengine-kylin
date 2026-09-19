#include "library.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

QList<WallpaperEntry> scanLibrary (const QString& workshopDir) {
    QList<WallpaperEntry> entries;

    QDir root (workshopDir);
    if (!root.exists ())
        return entries;

    QDirIterator it (workshopDir, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext ()) {
        const QString dirPath = it.next ();
        const QString projectPath = dirPath + "/project.json";
        QFile projectFile (projectPath);
        if (!projectFile.open (QIODevice::ReadOnly))
            continue;

        const QJsonObject project = QJsonDocument::fromJson (projectFile.readAll ()).object ();
        WallpaperEntry entry;
        entry.id = QFileInfo (dirPath).fileName ();
        entry.title = project.value ("title").toString (entry.id);
        entry.type = project.value ("type").toString ("unknown");

        const QString preview = dirPath + "/preview.jpg";
        entry.previewPath = QFile::exists (preview) ? preview : QString();

        entries.append (entry);
    }

    std::sort (entries.begin (), entries.end (), [] (const WallpaperEntry& a, const WallpaperEntry& b) {
        return a.title.compare (b.title, Qt::CaseInsensitive) < 0;
    });
    return entries;
}
