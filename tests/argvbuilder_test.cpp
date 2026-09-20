// T0 pure-logic tests: Config -> engine argv mapping.
#include "../src/service/argvbuilder.h"

#include <QtTest>

#include <algorithm>

namespace {
bool contains (const std::vector<std::string>& argv, const std::string& arg) {
    return std::find (argv.begin (), argv.end (), arg) != argv.end ();
}

int indexOf (const std::vector<std::string>& argv, const std::string& arg) {
    const auto it = std::find (argv.begin (), argv.end (), arg);
    return it == argv.end () ? -1 : static_cast<int> (it - argv.begin ());
}
} // namespace

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
        c.screens["DP-0"] = "843532366";
        const std::vector<std::string> argv = buildArgv (c);
        const int root = indexOf (argv, "--screen-root");
        const int bg = indexOf (argv, "--bg");
        QVERIFY (root > 0 && bg == root + 2);
        QCOMPARE (argv.at (root + 1), std::string ("DP-0"));
        QCOMPARE (argv.at (bg + 1), std::string ("843532366"));
    }

    void scalingAndClampFollowTheScreen () {
        Config c = defaultConfig ();
        c.screens["DP-0"] = "843532366";
        const std::vector<std::string> argv = buildArgv (c);
        const int root = indexOf (argv, "--screen-root");
        QCOMPARE (argv.at (root + 3), std::string ("843532366"));
        QCOMPARE (argv.at (root + 4), std::string ("--scaling"));
        QCOMPARE (argv.at (root + 5), std::string ("fill"));
        QCOMPARE (argv.at (root + 6), std::string ("--clamp"));
        QCOMPARE (argv.at (root + 7), std::string ("border"));
    }

    void assetsDirIsPassed () {
        const std::vector<std::string> argv = buildArgv (defaultConfig ());
        const int i = indexOf (argv, "--assets-dir");
        QVERIFY (i > 0);
        QCOMPARE (argv.at (i + 1), std::string ("/opt/assets"));
    }

    void silentExcludesVolume () {
        Config c = defaultConfig ();
        c.silent = true;
        QVERIFY (contains (buildArgv (c), "--silent"));
        QVERIFY (!contains (buildArgv (c), "--volume"));
        c.silent = false;
        c.volume = 42;
        const std::vector<std::string> argv = buildArgv (c);
        QVERIFY (!contains (argv, "--silent"));
        QVERIFY (contains (argv, "--volume"));
        QVERIFY (contains (argv, "42"));
    }

    void fullscreenPauseEmitsNegatedFlag () {
        Config c = defaultConfig ();
        c.fullscreenPause = false;
        QVERIFY (contains (buildArgv (c), "--no-fullscreen-pause"));
        c.fullscreenPause = true;
        QVERIFY (!contains (buildArgv (c), "--no-fullscreen-pause"));
    }

    void disableFlagsRespectValues () {
        Config c = defaultConfig ();
        QVERIFY (!contains (buildArgv (c), "--disable-particles"));
        QVERIFY (!contains (buildArgv (c), "--disable-mouse"));
        QVERIFY (!contains (buildArgv (c), "--disable-parallax"));
        c.disableParticles = c.disableMouse = c.disableParallax = true;
        const std::vector<std::string> argv = buildArgv (c);
        QVERIFY (contains (argv, "--disable-particles"));
        QVERIFY (contains (argv, "--disable-mouse"));
        QVERIFY (contains (argv, "--disable-parallax"));
    }

    void fpsIsEmitted () {
        Config c = defaultConfig ();
        c.fps = 60;
        const std::vector<std::string> argv = buildArgv (c);
        const int i = indexOf (argv, "--fps");
        QVERIFY (i > 0);
        QCOMPARE (argv.at (i + 1), std::string ("60"));
    }

    void setPropertyIsRenderedForActiveWallpaper () {
        Config c = defaultConfig ();
        c.screens["DP-0"] = "843532366";
        c.properties["843532366"]["schemecolor"] = "0.1 0.2 0.3";
        const std::vector<std::string> argv = buildArgv (c);
        const int i = indexOf (argv, "--set-property");
        QVERIFY (i > 0);
        QCOMPARE (argv.at (i + 1), std::string ("schemecolor=0.1 0.2 0.3"));
    }

    void propertiesOfInactiveWallpapersAreFiltered () {
        Config c = defaultConfig ();
        c.screens["DP-0"] = "843532366";
        c.properties["843532366"]["bloom"] = "1";
        // schemecolor exists in many wallpapers; a value set for a wallpaper
        // that is not being launched must not leak into this launch
        c.properties["999999999"]["schemecolor"] = "1 0 0";
        const std::vector<std::string> argv = buildArgv (c);
        QVERIFY (contains (argv, "bloom=1"));
        QVERIFY (!contains (argv, "schemecolor=1 0 0"));
    }

    void automuteAndAudioProcessingEmitNegatedFlags () {
        const Config c = defaultConfig (); // both on by default: no flags
        QVERIFY (!contains (buildArgv (c), "--noautomute"));
        QVERIFY (!contains (buildArgv (c), "--no-audio-processing"));
        Config disabled = defaultConfig ();
        disabled.automute = false;
        disabled.audioProcessing = false;
        const std::vector<std::string> argv = buildArgv (disabled);
        QVERIFY (contains (argv, "--noautomute"));
        QVERIFY (contains (argv, "--no-audio-processing"));
    }
};

QTEST_MAIN (ArgvBuilderTest)
#include "argvbuilder_test.moc"
