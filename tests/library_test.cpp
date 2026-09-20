// Library scan tests: pure metadata — directories under a temp workshop
// root, no image decoding involved (the service only resolves preview
// paths; decoding is a frontend concern).
#include "../src/library.h"

#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

namespace {

void put (const std::string& path, const std::string& content) {
    std::ofstream file (path, std::ios::trunc);
    QVERIFY (file.is_open ());
    file << content;
    QVERIFY (file.good ());
}

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
        const std::string dir = (m_workshop->path () + "/entry-a").toStdString ();
        QVERIFY (fs::create_directories (dir));
        const std::string json = R"({"title": "Beta Wall", "type": "scene", "preview": "cover.png"})";
        put (dir + "/project.json", json);
        put (dir + "/cover.png", std::string (4096, 'x'));
        put (dir + "/nested.bin", std::string (512, 'y')); // size walks subdirectories too
        const uint64_t expectedSize = json.size () + 4096 + 512;

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (1));
        QCOMPARE (entries.front ().id, std::string ("entry-a"));
        QCOMPARE (entries.front ().title, std::string ("Beta Wall"));
        QCOMPARE (entries.front ().type, std::string ("scene"));
        QCOMPARE (entries.front ().previewPath, dir + "/cover.png");
        QCOMPARE (entries.front ().sizeBytes, expectedSize);
    }

    void scan_fallsBackToPreviewJpg () {
        const std::string dir = (m_workshop->path () + "/entry-b").toStdString ();
        QVERIFY (fs::create_directories (dir));
        put (dir + "/project.json", R"({"title": "Jpg Fallback"})");
        put (dir + "/preview.jpg", std::string (16, 'p'));

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (1));
        QCOMPARE (entries.front ().previewPath, dir + "/preview.jpg");
    }

    void scan_absentPreviewYieldsEmptyPath () {
        const std::string dir = (m_workshop->path () + "/entry-c").toStdString ();
        QVERIFY (fs::create_directories (dir));
        put (dir + "/project.json", R"({"title": "No Preview", "preview": "missing.png"})");

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (1));
        QVERIFY (entries.front ().previewPath.empty ());
    }

    void scan_skipsDirectoriesWithoutProjectJson () {
        QVERIFY (fs::create_directories ((m_workshop->path () + "/plain-dir").toStdString ()));
        put ((m_workshop->path () + "/plain-dir/preview.jpg").toStdString (), std::string (8, 'p'));
        const std::string withProject = (m_workshop->path () + "/real-entry").toStdString ();
        QVERIFY (fs::create_directories (withProject));
        put (withProject + "/project.json", R"({"title": "Real"})");

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (1));
        QCOMPARE (entries.front ().id, std::string ("real-entry"));
    }

    void scan_defaultsTitleToIdAndTypeToUnknown () {
        const std::string dir = (m_workshop->path () + "/bare").toStdString ();
        QVERIFY (fs::create_directories (dir));
        put (dir + "/project.json", "{}");

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (1));
        QCOMPARE (entries.front ().title, std::string ("bare"));
        QCOMPARE (entries.front ().type, std::string ("unknown"));
    }

    void scan_sortsByTitleCaseInsensitive () {
        // created in reverse alphabetical order to prove the sort applies
        struct { const char* dir; const char* title; } cases[] = {
            { "z-dir", "Zebra" },
            { "a-dir", "apple" }, // case-insensitively before Zebra
        };
        for (const auto& c : cases) {
            const std::string dir = (m_workshop->path () + "/" + c.dir).toStdString ();
            QVERIFY (fs::create_directories (dir));
            put (dir + "/project.json", std::string (R"({"title": ")") + c.title + "\"}");
        }

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (2));
        QCOMPARE (entries.at (0).title, std::string ("apple"));
        QCOMPARE (entries.at (1).title, std::string ("Zebra"));
    }

    void scan_missingRootYieldsEmpty () {
        QVERIFY (scanLibrary ((m_workshop->path () + "/does-not-exist").toStdString ()).empty ());
    }

    void scan_rejectsPreviewEscapingTheWallpaperDir () {
        // project.json is downloaded content: a declared preview of
        // "../../../../etc/passwd" must not become a path a frontend serves
        const std::string dir = (m_workshop->path () + "/entry-escape").toStdString ();
        QVERIFY (fs::create_directories (dir));
        put (dir + "/project.json", R"({"title": "Escape", "preview": "../../../../etc/passwd"})");

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (1));
        QVERIFY (entries.front ().previewPath.empty ());
    }

    void scan_rejectsPreviewSymlinkedOutOfTheWallpaperDir () {
        // a symlink inside the wallpaper pointing elsewhere in the workshop
        // root resolves outside the entry: canonicalization has to catch it
        const std::string outside = (m_workshop->path () + "/outside.png").toStdString ();
        put (outside, std::string (8, 'o'));
        const std::string dir = (m_workshop->path () + "/entry-link").toStdString ();
        QVERIFY (fs::create_directories (dir));
        put (dir + "/project.json", R"({"title": "Link", "preview": "cover.png"})");
        std::error_code ec;
        fs::create_symlink (outside, dir + "/cover.png", ec);
        QVERIFY (!ec);

        const std::vector<WallpaperEntry> entries = scanLibrary (m_workshop->path ().toStdString ());
        QCOMPARE (entries.size (), size_t (1));
        QVERIFY (entries.front ().previewPath.empty ());
    }

private:
    std::unique_ptr<QTemporaryDir> m_workshop;
};

QTEST_MAIN (LibraryTest)
#include "library_test.moc"
