// T0 pure-logic tests: Config persistence (JSON roundtrip, isolation).
// XDG_CONFIG_HOME is pointed at a temp directory in initTestCase so these
// tests never touch the real ~/.config.
#include "../src/config.h"

#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

class ConfigTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QTemporaryDir* dir = new QTemporaryDir(); // freed at process exit: spans the whole run
        QVERIFY(dir->isValid());
        m_configHome = dir->path();
        qputenv("XDG_CONFIG_HOME", m_configHome.toUtf8());
    }

    void saveCreatesConfigFile() {
        Config c;
        QVERIFY(c.save());
        QVERIFY(std::filesystem::exists(Config::configPath()));
    }

    void roundtrip_preservesFields() {
        Config written;
        written.enginePath = "/opt/engine";
        written.display = ":1";
        written.fps = 60;
        written.silent = false;
        written.volume = 42;
        written.automute = false;
        written.audioProcessing = false;
        written.screens["DP-0"] = "123456";
        QVERIFY(written.save());

        const Config read = Config::load();
        QCOMPARE(read.enginePath, written.enginePath);
        QCOMPARE(read.display, written.display);
        QCOMPARE(read.fps, 60);
        QCOMPARE(read.silent, false);
        QCOMPARE(read.volume, 42);
        QCOMPARE(read.automute, false);
        QCOMPARE(read.audioProcessing, false);
        QCOMPARE(read.screens.at("DP-0"), std::string("123456"));
    }

    void unknownJsonKeysAreIgnored() {
        // forward/backward compatibility: extra keys must not break loading
        Config c;
        c.save();
        QFile f(QString::fromStdString(Config::configPath()));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonObject extended = obj;
        extended.insert("someFutureKey", "whatever");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(extended).toJson());
        f.close();

        const Config read = Config::load();
        QCOMPARE(read.fps, c.fps);
    }

    void loadOnMissingFileGivesDefaults() {
        Config c;
        c.fps = 99;
        QVERIFY(c.save());
        std::filesystem::remove(Config::configPath());
        const Config fresh = Config::load();
        QCOMPARE(fresh.fps, 30);
        QVERIFY(fresh.silent);
    }

    void loadOnMissingFileIsNotAnError() {
        // the first-run case: defaults are the answer, not a failure
        std::filesystem::remove(Config::configPath());
        wallpaper_engine::Error error;
        Config::load(&error);
        QCOMPARE(error.kind, wallpaper_engine::Error::NoError);
    }

    void loadOnCorruptFileReportsItButKeepsDefaults() {
        Config c;
        c.fps = 77;
        QVERIFY(c.save());
        QFile f(QString::fromStdString(Config::configPath()));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{ this is not json");
        f.close();

        wallpaper_engine::Error error;
        const Config read = Config::load(&error);
        QCOMPARE(error.kind, wallpaper_engine::Error::CorruptConfig);
        QVERIFY(!error.message.empty());
        QCOMPARE(read.fps, 30); // defaults survive, but now they are explained
    }

    void saveIsAtomicAndLeavesNoTempFile() {
        Config c;
        QVERIFY(c.save());
        QVERIFY(!std::filesystem::exists(Config::configPath() + ".tmp"));
    }

    void failedSaveReportsFileError() {
        // a config path whose parent is a file, not a directory: the write
        // cannot succeed, and the caller learns why instead of getting a
        // bare false
        const QByteArray blocked = (m_configHome + "/blocked").toUtf8(); // inside the temp dir
        QFile blocker(QString::fromUtf8(blocked));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.write("not a directory");
        blocker.close();

        qputenv("XDG_CONFIG_HOME", blocked + "/sub");
        Config c;
        wallpaper_engine::Error error;
        const bool saved = c.save(&error);
        qputenv("XDG_CONFIG_HOME", m_configHome.toUtf8()); // restore before asserting

        QVERIFY(!saved);
        QCOMPARE(error.kind, wallpaper_engine::Error::FileError);
        QVERIFY(!error.message.empty());
    }

private:
    QString m_configHome;
};

QTEST_MAIN(ConfigTest)
#include "config_test.moc"
