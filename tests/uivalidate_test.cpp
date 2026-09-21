// T0 pure-logic tests: the local server's security boundary. Everything the
// server decides before it looks at a route lives here, so these are the
// assertions that must hold no matter how the handlers change.
#include "../src/uivalidate.h"

#include <QtTest>

#include <string>

class UiValidateTest : public QObject {
    Q_OBJECT

private slots:
    void tokenMatchesOnlyTheExactToken() {
        QVERIFY(Ui::tokenMatches("a1b2c3", "a1b2c3"));
        QVERIFY(!Ui::tokenMatches("a1b2c4", "a1b2c3"));
        QVERIFY(!Ui::tokenMatches("a1b2c", "a1b2c3"));   // prefix is not a match
        QVERIFY(!Ui::tokenMatches("a1b2c3d", "a1b2c3")); // nor is an extension
        QVERIFY(!Ui::tokenMatches("", "a1b2c3"));
    }

    void noConfiguredTokenNeverMatches() {
        // a server that failed to generate a token must reject everything
        // rather than accept everything
        QVERIFY(!Ui::tokenMatches("", ""));
        QVERIFY(!Ui::tokenMatches("anything", ""));
    }

    void hostHeaderMustNameThisServer() {
        QVERIFY(Ui::hostAllowed("127.0.0.1:8080", 8080));
        QVERIFY(Ui::hostAllowed("localhost:8080", 8080));
        QVERIFY(Ui::hostAllowed("[::1]:8080", 8080));
    }

    void hostHeaderRejectsEverythingElse() {
        QVERIFY(!Ui::hostAllowed("127.0.0.1:9999", 8080));      // another server
        QVERIFY(!Ui::hostAllowed("127.0.0.1.evil.com:8080", 8080));
        QVERIFY(!Ui::hostAllowed("evil.com:8080", 8080));
        QVERIFY(!Ui::hostAllowed("", 8080));
    }

    void sessionCookieIsFoundAmongOthers() {
        QCOMPARE(Ui::sessionTokenFromCookie("other=1; wallpaper_engine_session=deadbeef; theme=dark", "wallpaper_engine_session"),
                  std::string("deadbeef"));
        QCOMPARE(Ui::sessionTokenFromCookie("wallpaper_engine_session=only", "wallpaper_engine_session"), std::string("only"));
        QCOMPARE(Ui::sessionTokenFromCookie("wallpaper_engine_session=trailing;", "wallpaper_engine_session"), std::string("trailing"));
    }

    void sessionCookieRejectsNearMisses() {
        QCOMPARE(Ui::sessionTokenFromCookie("", "wallpaper_engine_session"), std::string());
        QCOMPARE(Ui::sessionTokenFromCookie("other=1", "wallpaper_engine_session"), std::string());
        // a cookie whose name merely ends with ours is not ours
        QCOMPARE(Ui::sessionTokenFromCookie("not_wallpaper_engine_session=nope", "wallpaper_engine_session"), std::string());
    }

    void wallpaperIdMustBeASingleComponent() {
        QVERIFY(Ui::isSafeWallpaperId("843532366"));
        QVERIFY(Ui::isSafeWallpaperId("entry-a_1.2"));
    }

    void wallpaperIdRejectsTraversal() {
        QVERIFY(!Ui::isSafeWallpaperId(""));
        QVERIFY(!Ui::isSafeWallpaperId("."));
        QVERIFY(!Ui::isSafeWallpaperId(".."));
        QVERIFY(!Ui::isSafeWallpaperId("a/b"));
        QVERIFY(!Ui::isSafeWallpaperId("a\\b"));
        QVERIFY(!Ui::isSafeWallpaperId("a\nb"));
        QVERIFY(!Ui::isSafeWallpaperId(std::string("a\0b", 3)));
        QVERIFY(!Ui::isSafeWallpaperId(std::string(65, 'x')));
    }
};

QTEST_MAIN(UiValidateTest)
#include "uivalidate_test.moc"
