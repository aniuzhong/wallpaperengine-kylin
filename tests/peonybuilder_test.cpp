// T0 pure-logic tests: the peony integration's pure half — the candidate
// list the shim matches wallpapers against, and the environment the
// injected peony is launched with. The syscall sequence that consumes them
// (integration.cpp) is not covered here.
#include "../src/peonybuilder.h"

#include <QtTest>

namespace {

// /proc/<pid>/cmdline is NUL-separated, not space-separated
std::string cmdline(const std::vector<std::string>& argv) {
    std::string joined;
    for (const std::string& arg : argv) {
        joined += arg;
        joined.push_back('\0');
    }
    return joined;
}

} // namespace

class PeonyBuilderTest : public QObject {
    Q_OBJECT

private slots:
    void cmdlineMatchesTheDesktopProcess() {
        QVERIFY(Peony::isPeonyDesktopCmdline(cmdline({ "/usr/bin/peony-qt-desktop", "-w", "-d" })));
        QVERIFY(Peony::isPeonyDesktopCmdline(cmdline({ "peony-qt-desktop" })));
    }

    void cmdlineRejectsEverythingElse() {
        QVERIFY(!Peony::isPeonyDesktopCmdline(cmdline({ "/usr/bin/peony", "--daemon" })));
        QVERIFY(!Peony::isPeonyDesktopCmdline(""));
    }

    void wallpaperListOrdersPreviousNormalizedMarker() {
        // the order the list was always built in: what the user had, then the
        // path accountsservice normalized the marker to, then the marker
        QCOMPARE(Peony::buildWallpaperList("/data/marker.png", "/var/lib/AccountsService/backgrounds/x.png",
                                                   "/old/wall.png"),
                  std::string("/old/wall.png:/var/lib/AccountsService/backgrounds/x.png:/data/marker.png"));
    }

    void wallpaperListDropsEmptyAndRepeatedEntries() {
        QCOMPARE(Peony::buildWallpaperList("/m.png", "", ""), std::string("/m.png"));
        // accountsservice handed back the marker itself: one entry, not two
        QCOMPARE(Peony::buildWallpaperList("/m.png", "/m.png", ""), std::string("/m.png"));
        // the accountsservice write failed: the previous wallpaper is the
        // only other candidate the shim may be asked about
        QCOMPARE(Peony::buildWallpaperList("/m.png", "", "/old.png"), std::string("/old.png:/m.png"));
    }

    void wallpaperListKeepsPathsThatMerelyContainEachOther() {
        // the substring test this replaced dropped the shorter path whenever
        // it happened to appear inside a longer one; the shim compares whole
        // paths, so both candidates must survive
        QCOMPARE(Peony::buildWallpaperList("/a/b.png.bak", "", "/a/b.png"),
                  std::string("/a/b.png:/a/b.png.bak"));
    }

    void firstWallpaperIsTheOneTheDesktopHad() {
        // the order buildWallpaperList produces: previous, normalized, marker
        QCOMPARE(Peony::firstWallpaperIn("/old/wall.png:/var/lib/AccountsService/backgrounds/x.png:/data/m.png"),
                  std::string("/old/wall.png"));
        QCOMPARE(Peony::firstWallpaperIn("/only.png"), std::string("/only.png"));
        QCOMPARE(Peony::firstWallpaperIn(""), std::string());
    }

    void shimEnvironmentCarriesTheThreeVariables() {
        const std::map<std::string, std::string> env =
            Peony::buildShimEnvironment("/opt/wallpaper-engine/lib/libpeony-alpha.so", "/a.png:/b.png", "/tmp/shim.log");

        QCOMPARE(env.size(), size_t(3));
        QCOMPARE(env.at("LD_PRELOAD"), std::string("/opt/wallpaper-engine/lib/libpeony-alpha.so"));
        QCOMPARE(env.at("PEONY_ALPHA_WALLPAPER"), std::string("/a.png:/b.png"));
        QCOMPARE(env.at("PEONY_ALPHA_LOG"), std::string("/tmp/shim.log"));
    }
};

QTEST_MAIN(PeonyBuilderTest)
#include "peonybuilder_test.moc"
