// T0 pure-logic tests: config::Config persistence (JSON roundtrip, isolation).
// XDG_CONFIG_HOME is pointed at a temp directory in initTestCase so these
// tests never touch the real ~/.config.
#include "../src/config.h"

#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <string>
#include <pwd.h>
#include <unistd.h>

namespace we {
namespace paths {

inline std::string HomeDir() {
    const char* home = getenv("HOME");
    if (home != nullptr && *home != '\0')
        return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return pw->pw_dir;
    return {};
}

inline std::string ConfigHome() {
    const char* configHome = getenv("XDG_CONFIG_HOME");
    if (configHome != nullptr && *configHome != '\0')
        return configHome;
    return HomeDir() + "/.config";
}

inline std::string ProductConfigDir() {
    return ConfigHome() + "/wallpaper-engine";
}

inline std::string ConfigFile() {
    return ProductConfigDir() + "/config.json";
}

} // namespace paths
} // namespace we

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
        config::Config c;
        QVERIFY(c.Save().has_value());
        QVERIFY(std::filesystem::exists(we::paths::ConfigFile()));
    }

    void roundtrip_preservesFields() {
        config::Config written;
        written.enginePath = "/opt/engine";
        written.display = ":1";
        written.fps = 60;
        written.silent = false;
        written.volume = 42;
        written.automute = false;
        written.audioProcessing = false;
        written.screens["DP-0"] = "123456";
        QVERIFY(written.Save().has_value());

        const config::Config read = config::Config::Load();
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
        config::Config c;
        c.Save();
        QFile f(QString::fromStdString(we::paths::ConfigFile()));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonObject extended = obj;
        extended.insert("someFutureKey", "whatever");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(extended).toJson());
        f.close();

        const config::Config read = config::Config::Load();
        QCOMPARE(read.fps, c.fps);
    }

    void loadOnMissingFileGivesDefaults() {
        config::Config c;
        c.fps = 99;
        QVERIFY(c.Save().has_value());
        std::filesystem::remove(we::paths::ConfigFile());
        const config::Config fresh = config::Config::Load();
        QCOMPARE(fresh.fps, 30);
        QVERIFY(fresh.silent);
    }

    void loadOnMissingFileIsNotAnError() {
        // the first-run case: defaults are the answer, not a failure
        std::filesystem::remove(we::paths::ConfigFile());
        we::Error error;
        config::Config::Load(&error);
        QCOMPARE(error.kind, we::Error::NoError);
    }

    void loadOnCorruptFileReportsItButKeepsDefaults() {
        config::Config c;
        c.fps = 77;
        QVERIFY(c.Save().has_value());
        QFile f(QString::fromStdString(we::paths::ConfigFile()));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{ this is not json");
        f.close();

        we::Error error;
        const config::Config read = config::Config::Load(&error);
        QCOMPARE(error.kind, we::Error::CorruptConfig);
        QVERIFY(!error.message.empty());
        QCOMPARE(read.fps, 30); // defaults survive, but now they are explained
    }

    void saveIsAtomicAndLeavesNoTempFile() {
        config::Config c;
        QVERIFY(c.Save().has_value());
        QVERIFY(!std::filesystem::exists(we::paths::ConfigFile() + ".tmp"));
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
        config::Config c;
        const auto saved = c.Save();
        qputenv("XDG_CONFIG_HOME", m_configHome.toUtf8()); // restore before asserting

        QVERIFY(!saved.has_value());
        QCOMPARE(saved.error().kind, we::Error::FileError);
        QVERIFY(!saved.error().message.empty());
    }

private:
    QString m_configHome;
};

QTEST_MAIN(ConfigTest)
#include "config_test.moc"
