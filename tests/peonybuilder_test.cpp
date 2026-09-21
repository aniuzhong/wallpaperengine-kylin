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
        QVERIFY(peony::IsPeonyDesktopCmdline(cmdline({ "/usr/bin/peony-qt-desktop", "-w", "-d" })));
        QVERIFY(peony::IsPeonyDesktopCmdline(cmdline({ "peony-qt-desktop" })));
    }

    void cmdlineRejectsEverythingElse() {
        QVERIFY(!peony::IsPeonyDesktopCmdline(cmdline({ "/usr/bin/peony", "--daemon" })));
        QVERIFY(!peony::IsPeonyDesktopCmdline(""));
    }

    void wallpaperListOrdersPreviousNormalizedMarker() {
        // the order the list was always built in: what the user had, then the
        // path accountsservice normalized the marker to, then the marker
        QCOMPARE(peony::BuildWallpaperList("/data/marker.png", "/var/lib/AccountsService/backgrounds/x.png",
                                                   "/old/wall.png"),
                  std::string("/old/wall.png:/var/lib/AccountsService/backgrounds/x.png:/data/marker.png"));
    }

    void wallpaperListDropsEmptyAndRepeatedEntries() {
        QCOMPARE(peony::BuildWallpaperList("/m.png", "", ""), std::string("/m.png"));
        // accountsservice handed back the marker itself: one entry, not two
        QCOMPARE(peony::BuildWallpaperList("/m.png", "/m.png", ""), std::string("/m.png"));
        // the accountsservice write failed: the previous wallpaper is the
        // only other candidate the shim may be asked about
        QCOMPARE(peony::BuildWallpaperList("/m.png", "", "/old.png"), std::string("/old.png:/m.png"));
    }

    void wallpaperListKeepsPathsThatMerelyContainEachOther() {
        // the substring test this replaced dropped the shorter path whenever
        // it happened to appear inside a longer one; the shim compares whole
        // paths, so both candidates must survive
        QCOMPARE(peony::BuildWallpaperList("/a/b.png.bak", "", "/a/b.png"),
                  std::string("/a/b.png:/a/b.png.bak"));
    }

    void firstWallpaperIsTheOneTheDesktopHad() {
        // the order BuildWallpaperList produces: previous, normalized, marker
        QCOMPARE(peony::FirstWallpaperIn("/old/wall.png:/var/lib/AccountsService/backgrounds/x.png:/data/m.png"),
                  std::string("/old/wall.png"));
        QCOMPARE(peony::FirstWallpaperIn("/only.png"), std::string("/only.png"));
        QCOMPARE(peony::FirstWallpaperIn(""), std::string());
    }

    void shimEnvironmentCarriesTheContractVariables() {
        // the log destination is not in the environment: shim logging is
        // always on and resolves its own per-user destination
        const std::map<std::string, std::string> env =
            peony::BuildShimEnvironment("/opt/wallpaper-engine/lib/libpeony-alpha.so", "/a.png:/b.png");

        QCOMPARE(env.size(), size_t(2));
        QCOMPARE(env.at("LD_PRELOAD"), std::string("/opt/wallpaper-engine/lib/libpeony-alpha.so"));
        QCOMPARE(env.at("PEONY_ALPHA_WALLPAPER"), std::string("/a.png:/b.png"));
    }
};

QTEST_MAIN(PeonyBuilderTest)
#include "peonybuilder_test.moc"
