// Library scan tests: pure metadata — directories under a temp workshop
// root, no image decoding involved (the service only resolves preview
// paths; decoding is a frontend concern).
#include "../src/service/library.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>
#include <QtTest>

namespace {

void put (const QString& path, const QByteArray& content) {
    QFile file (path);
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (content), qint64 (content.size ()));
}

QByteArray projectJson (const char* json) { return QByteArray (json); }

} // namespace

class LibraryTest : public QObject {
    Q_OBJECT

private slots:
    void init () {
        // a fresh workshop root per test: scanLibrary must only see what the
        // running test created
        m_workshop = std::make_unique<QTemporaryDir> ();
        QVERIFY (m_workshop->isValid ());
    }

    void scan_readsFullMetadata () {
        const QString dir = m_workshop->path () + "/entry-a";
        QVERIFY (QDir ().mkpath (dir));
        const QByteArray json = R"({"title": "Beta Wall", "type": "scene", "preview": "cover.png"})";
        put (dir + "/project.json", json);
        put (dir + "/cover.png", QByteArray (4096, 'x'));
        put (dir + "/nested.bin", QByteArray (512, 'y')); // size walks subdirectories too
        const qint64 expectedSize = json.size () + 4096 + 512;

        const QList<WallpaperEntry> entries = scanLibrary (m_workshop->path ());
        QCOMPARE (entries.size (), 1);
        QCOMPARE (entries.first ().id, QString ("entry-a"));
        QCOMPARE (entries.first ().title, QString ("Beta Wall"));
        QCOMPARE (entries.first ().type, QString ("scene"));
        QCOMPARE (entries.first ().previewPath, dir + "/cover.png");
        QCOMPARE (qint64 (entries.first ().sizeBytes), expectedSize);
    }

    void scan_fallsBackToPreviewJpg () {
        const QString dir = m_workshop->path () + "/entry-b";
        QVERIFY (QDir ().mkpath (dir));
        put (dir + "/project.json", projectJson (R"({"title": "Jpg Fallback"})"));
        put (dir + "/preview.jpg", QByteArray (16, 'p'));

        const QList<WallpaperEntry> entries = scanLibrary (m_workshop->path ());
        QCOMPARE (entries.size (), 1);
        QCOMPARE (entries.first ().previewPath, dir + "/preview.jpg");
    }

    void scan_absentPreviewYieldsEmptyPath () {
        const QString dir = m_workshop->path () + "/entry-c";
        QVERIFY (QDir ().mkpath (dir));
        put (dir + "/project.json", projectJson (R"({"title": "No Preview", "preview": "missing.png"})"));

        const QList<WallpaperEntry> entries = scanLibrary (m_workshop->path ());
        QCOMPARE (entries.size (), 1);
        QVERIFY (entries.first ().previewPath.isEmpty ());
    }

    void scan_skipsDirectoriesWithoutProjectJson () {
        QVERIFY (QDir ().mkpath (m_workshop->path () + "/plain-dir"));
        put (m_workshop->path () + "/plain-dir/preview.jpg", QByteArray (8, 'p'));
        const QString withProject = m_workshop->path () + "/real-entry";
        QVERIFY (QDir ().mkpath (withProject));
        put (withProject + "/project.json", projectJson (R"({"title": "Real"})"));

        const QList<WallpaperEntry> entries = scanLibrary (m_workshop->path ());
        QCOMPARE (entries.size (), 1);
        QCOMPARE (entries.first ().id, QString ("real-entry"));
    }

    void scan_defaultsTitleToIdAndTypeToUnknown () {
        const QString dir = m_workshop->path () + "/bare";
        QVERIFY (QDir ().mkpath (dir));
        put (dir + "/project.json", projectJson ("{}"));

        const QList<WallpaperEntry> entries = scanLibrary (m_workshop->path ());
        QCOMPARE (entries.size (), 1);
        QCOMPARE (entries.first ().title, QString ("bare"));
        QCOMPARE (entries.first ().type, QString ("unknown"));
    }

    void scan_sortsByTitleCaseInsensitive () {
        // created in reverse alphabetical order to prove the sort applies
        struct { const char* dir; const char* title; } cases[] = {
            { "z-dir", "Zebra" },
            { "a-dir", "apple" }, // case-insensitively before Zebra
        };
        for (const auto& c : cases) {
            const QString dir = m_workshop->path () + "/" + c.dir;
            QVERIFY (QDir ().mkpath (dir));
            put (dir + "/project.json", projectJson (QByteArray (R"({"title": ")") + c.title + "\"}"));
        }

        const QList<WallpaperEntry> entries = scanLibrary (m_workshop->path ());
        QCOMPARE (entries.size (), 2);
        QCOMPARE (entries.at (0).title, QString ("apple"));
        QCOMPARE (entries.at (1).title, QString ("Zebra"));
    }

    void scan_missingRootYieldsEmpty () {
        QVERIFY (scanLibrary (m_workshop->path () + "/does-not-exist").isEmpty ());
    }

private:
    std::unique_ptr<QTemporaryDir> m_workshop;
};

QTEST_MAIN (LibraryTest)
#include "library_test.moc"
