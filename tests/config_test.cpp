// T0 pure-logic tests: Config persistence (JSON roundtrip, isolation).
// QStandardPaths::setTestModeEnabled redirects GenericConfigLocation away
// from the real ~/.config so these tests never touch user state.
#include "../src/ui/config.h"

#include <QFile>
#include <QStandardPaths>
#include <QtTest>

class ConfigTest : public QObject {
    Q_OBJECT

private slots:
    void init () { QStandardPaths::setTestModeEnabled (true); }

    void saveCreatesConfigFile () {
        Config c;
        QVERIFY (c.save ());
        QVERIFY (QFile::exists (Config::configPath ()));
    }

    void roundtrip_preservesFields () {
        Config written;
        written.enginePath = "/opt/engine";
        written.display = ":1";
        written.fps = 60;
        written.silent = false;
        written.volume = 42;
        written.automute = false;
        written.audioProcessing = false;
        written.screens.insert ("DP-0", "123456");
        QVERIFY (written.save ());

        const Config read = Config::load ();
        QCOMPARE (read.enginePath, written.enginePath);
        QCOMPARE (read.display, written.display);
        QCOMPARE (read.fps, 60);
        QCOMPARE (read.silent, false);
        QCOMPARE (read.volume, 42);
        QCOMPARE (read.automute, false);
        QCOMPARE (read.audioProcessing, false);
        QCOMPARE (read.screens.value ("DP-0"), QString ("123456"));
    }

    void unknownJsonKeysAreIgnored () {
        // forward/backward compatibility: extra keys must not break loading
        Config c;
        c.save ();
        QFile f (Config::configPath ());
        QVERIFY (f.open (QIODevice::ReadOnly));
        const QJsonObject obj = QJsonDocument::fromJson (f.readAll ()).object ();
        f.close ();
        QJsonObject extended = obj;
        extended.insert ("someFutureKey", "whatever");
        QVERIFY (f.open (QIODevice::WriteOnly | QIODevice::Truncate));
        f.write (QJsonDocument (extended).toJson ());
        f.close ();

        const Config read = Config::load ();
        QCOMPARE (read.fps, c.fps);
    }

    void loadOnMissingFileGivesDefaults () {
        Config c;
        c.fps = 99;
        QVERIFY (c.save ());
        QFile::remove (Config::configPath ());
        const Config fresh = Config::load ();
        QCOMPARE (fresh.fps, 30);
        QVERIFY (fresh.silent);
    }
};

QTEST_MAIN (ConfigTest)
#include "config_test.moc"
