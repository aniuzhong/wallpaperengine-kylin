// T0 pure-logic tests: Config -> engine argv mapping.
#include "../src/ui/argvbuilder.h"

#include <QtTest>

class ArgvBuilderTest : public QObject {
    Q_OBJECT

private:
    Config defaultConfig () const {
        Config c;
        c.enginePath = "/opt/engine";
        c.assetsDir = "/opt/assets";
        c.scaling = "fill";
        c.clamp = "border";
        c.fps = 30;
        c.fullscreenPause = true;
        c.silent = true;
        return c;
    }

private slots:
    void screenRootAndBgArePaired () {
        Config c = defaultConfig ();
        c.screens.insert ("DP-0", "843532366");
        const QStringList argv = buildArgv (c);
        const int root = argv.indexOf ("--screen-root");
        const int bg = argv.indexOf ("--bg");
        QVERIFY (root > 0 && bg == root + 2);
        QCOMPARE (argv.at (root + 1), QString ("DP-0"));
        QCOMPARE (argv.at (bg + 1), QString ("843532366"));
    }

    void scalingAndClampFollowTheScreen () {
        Config c = defaultConfig ();
        c.screens.insert ("DP-0", "843532366");
        const QStringList argv = buildArgv (c);
        const int root = argv.indexOf ("--screen-root");
        QCOMPARE (argv.at (root + 3), QString ("843532366"));
        QCOMPARE (argv.at (root + 4), QString ("--scaling"));
        QCOMPARE (argv.at (root + 5), QString ("fill"));
        QCOMPARE (argv.at (root + 6), QString ("--clamp"));
        QCOMPARE (argv.at (root + 7), QString ("border"));
    }

    void assetsDirIsPassed () {
        const QStringList argv = buildArgv (defaultConfig ());
        const int i = argv.indexOf ("--assets-dir");
        QVERIFY (i > 0);
        QCOMPARE (argv.at (i + 1), QString ("/opt/assets"));
    }

    void silentExcludesVolume () {
        Config c = defaultConfig ();
        c.silent = true;
        QVERIFY (buildArgv (c).contains ("--silent"));
        QVERIFY (!buildArgv (c).contains ("--volume"));
        c.silent = false;
        c.volume = 42;
        const QStringList argv = buildArgv (c);
        QVERIFY (!argv.contains ("--silent"));
        QVERIFY (argv.contains ("--volume"));
        QVERIFY (argv.contains ("42"));
    }

    void fullscreenPauseEmitsNegatedFlag () {
        Config c = defaultConfig ();
        c.fullscreenPause = false;
        QVERIFY (buildArgv (c).contains ("--no-fullscreen-pause"));
        c.fullscreenPause = true;
        QVERIFY (!buildArgv (c).contains ("--no-fullscreen-pause"));
    }

    void disableFlagsRespectValues () {
        Config c = defaultConfig ();
        QVERIFY (!buildArgv (c).contains ("--disable-particles"));
        QVERIFY (!buildArgv (c).contains ("--disable-mouse"));
        QVERIFY (!buildArgv (c).contains ("--disable-parallax"));
        c.disableParticles = c.disableMouse = c.disableParallax = true;
        const QStringList argv = buildArgv (c);
        QVERIFY (argv.contains ("--disable-particles"));
        QVERIFY (argv.contains ("--disable-mouse"));
        QVERIFY (argv.contains ("--disable-parallax"));
    }

    void fpsIsEmitted () {
        Config c = defaultConfig ();
        c.fps = 60;
        const QStringList argv = buildArgv (c);
        const int i = argv.indexOf ("--fps");
        QVERIFY (i > 0);
        QCOMPARE (argv.at (i + 1), QString ("60"));
    }

    void setPropertyIsRenderedForActiveWallpaper () {
        Config c = defaultConfig ();
        c.screens.insert ("DP-0", "843532366");
        c.properties.insert ("843532366", QVariantMap { { "schemecolor", "0.1 0.2 0.3" } });
        const QStringList argv = buildArgv (c);
        const int i = argv.indexOf ("--set-property");
        QVERIFY (i > 0);
        QCOMPARE (argv.at (i + 1), QString ("schemecolor=0.1 0.2 0.3"));
    }

    void propertiesOfInactiveWallpapersAreFiltered () {
        Config c = defaultConfig ();
        c.screens.insert ("DP-0", "843532366");
        c.properties.insert ("843532366", QVariantMap { { "bloom", "1" } });
        // schemecolor exists in many wallpapers; a value set for a wallpaper
        // that is not being launched must not leak into this launch
        c.properties.insert ("999999999", QVariantMap { { "schemecolor", "1 0 0" } });
        const QStringList argv = buildArgv (c);
        QVERIFY (argv.contains ("bloom=1"));
        QVERIFY (!argv.contains ("schemecolor=1 0 0"));
    }

    void automuteAndAudioProcessingEmitNegatedFlags () {
        const Config c = defaultConfig (); // both on by default: no flags
        QVERIFY (!buildArgv (c).contains ("--noautomute"));
        QVERIFY (!buildArgv (c).contains ("--no-audio-processing"));
        Config disabled = defaultConfig ();
        disabled.automute = false;
        disabled.audioProcessing = false;
        const QStringList argv = buildArgv (disabled);
        QVERIFY (argv.contains ("--noautomute"));
        QVERIFY (argv.contains ("--no-audio-processing"));
    }

    void commandLineQuotesArgumentsWithSpaces () {
        Config c = defaultConfig ();
        c.assetsDir = "/opt/some assets";
        const QString line = buildCommandLine (c);
        QVERIFY (line.contains ("\"/opt/some assets\""));
    }
};

QTEST_MAIN (ArgvBuilderTest)
#include "argvbuilder_test.moc"
