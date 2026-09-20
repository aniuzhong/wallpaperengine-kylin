// T0 pure-logic tests: turning the desktop's own browser association into a
// command line. This is where the Kylin desktop's variety shows up — the
// default browser here is a Flatpak whose Exec line is a `flatpak run`, and
// its desktop file carries action groups whose Execs must not be mistaken
// for the default one.
#include "../src/uilaunch.h"

#include <QtTest>

#include <algorithm>
#include <string>
#include <vector>

namespace {

bool contains (const std::vector<std::string>& argv, const std::string& needle) {
    return std::find (argv.begin (), argv.end (), needle) != argv.end ();
}

} // namespace

class UiLaunchTest : public QObject {
    Q_OBJECT

private slots:
    void desktopExecComesFromTheMainGroup () {
        // Kylin's qaxbrowser entry really does ship action groups
        const std::string desktop =
            "[Desktop Entry]\n"
            "Name=Qaxbrowser\n"
            "Exec=/usr/bin/qaxbrowser-safe-stable %U\n"
            "MimeType=text/html;\n"
            "\n"
            "[Desktop Action new-window]\n"
            "Name=New Window\n"
            "Exec=/usr/bin/qaxbrowser-safe-stable\n"
            "\n"
            "[Desktop Action new-private-window]\n"
            "Exec=/usr/bin/qaxbrowser-safe-stable --incognito\n";

        QCOMPARE (Ui::parseDesktopExec (desktop), std::string ("/usr/bin/qaxbrowser-safe-stable %U"));
    }

    void missingMainGroupYieldsNothing () {
        QVERIFY (Ui::parseDesktopExec ("[Desktop Action x]\nExec=/bin/false\n").empty ());
        QVERIFY (Ui::parseDesktopExec ("").empty ());
    }

    void execLineBecomesArgv () {
        const std::vector<std::string> argv = Ui::parseExecLine (
            "/usr/bin/flatpak run --branch=stable --arch=x86_64 --command=chrome com.google.Chrome %U");
        QCOMPARE (argv.size (), size_t (7));
        QCOMPARE (argv.front (), std::string ("/usr/bin/flatpak"));
        QCOMPARE (argv.at (1), std::string ("run"));
        QCOMPARE (argv.at (5), std::string ("com.google.Chrome"));
        QCOMPARE (argv.back (), std::string ("%U"));
    }

    void quotedArgumentsSurvive () {
        // a browser installed under a path with a space in it
        const std::vector<std::string> argv = Ui::parseExecLine ("\"/opt/my browser/browser\" --new-window %U");
        QCOMPARE (argv.size (), size_t (3));
        QCOMPARE (argv.front (), std::string ("/opt/my browser/browser"));
        QCOMPARE (argv.at (1), std::string ("--new-window"));
    }

    void chromiumFamilyIsDetected () {
        QVERIFY (Ui::isChromiumFamily ("/usr/bin/google-chrome-stable %U"));
        QVERIFY (Ui::isChromiumFamily ("/usr/bin/flatpak run --command=chrome com.google.Chrome %U"));
        QVERIFY (Ui::isChromiumFamily ("/usr/bin/qaxbrowser-safe-stable %U"));
        QVERIFY (Ui::isChromiumFamily ("/usr/bin/microsoft-edge %U"));
    }

    void firefoxIsNotChromium () {
        QVERIFY (!Ui::isChromiumFamily ("/usr/bin/firefox %u"));
        QVERIFY (!Ui::isChromiumFamily ("/home/someone/software/firefox-154.0/firefox/firefox-bin %u"));
    }

    void chromiumBrowserGetsAnAppWindow () {
        const Ui::Launch launch = Ui::launchFor (
            "/usr/bin/flatpak run --branch=stable --command=chrome com.google.Chrome %U", "http://127.0.0.1:4321/");

        QVERIFY (launch.appWindow);
        QCOMPARE (launch.argv.back (), std::string ("--app=http://127.0.0.1:4321/"));
        QVERIFY (!contains (launch.argv, "%U")); // the field code is consumed, not passed through
    }

    void otherBrowserJustGetsTheUrl () {
        const Ui::Launch launch = Ui::launchFor ("/usr/bin/firefox %u", "http://127.0.0.1:4321/");

        QVERIFY (!launch.appWindow);
        QCOMPARE (launch.argv.size (), size_t (2));
        QCOMPARE (launch.argv.back (), std::string ("http://127.0.0.1:4321/"));
    }

    void fieldCodesAreStrippedWhereverTheyAppear () {
        // %u inside a longer argument loses the code but keeps the argument;
        // %% is a literal percent and survives
        const Ui::Launch launch =
            Ui::launchFor ("/usr/bin/firefox --profile=%u \"100%%zoom\"", "http://127.0.0.1:1/");
        QCOMPARE (launch.argv.at (1), std::string ("--profile="));
        QCOMPARE (launch.argv.at (2), std::string ("100%zoom"));
    }

    void emptyExecLineYieldsNoCommand () {
        QVERIFY (Ui::launchFor ("", "http://127.0.0.1:1/").argv.empty ());
        QVERIFY (Ui::launchFor ("   ", "http://127.0.0.1:1/").argv.empty ());
    }
};

QTEST_MAIN (UiLaunchTest)
#include "uilaunch_test.moc"
