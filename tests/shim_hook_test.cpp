// Shim hook coverage: launches the probe binary with LD_PRELOAD set and
// asserts the two shim behaviors end-to-end.
//   ctor hook    -> the wallpaper pixmap is replaced with transparency
//   xcb property -> DESKTOP window type is rewritten to NORMAL
// The xcb mode requires an X server and is skipped without DISPLAY.
#include <QImage>
#include <QColor>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtTest>

namespace {
constexpr const char* kShimPath = SHIM_PATH;
constexpr const char* kProbePath = PROBE_PATH;
constexpr const char* kMarkerPath = MARKER_PATH;
} // namespace

class ShimHookTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase () {
        QImage image (64, 64, QImage::Format_RGB888);
        image.fill (QColor (255, 0, 255));
        QVERIFY (image.save (kMarkerPath));
        QVERIFY (QFile::exists (SHIM_PATH));
    }

    void ctorHook_nullifiesWallpaper () {
        QProcess probe;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment ();
        env.insert ("LD_PRELOAD", kShimPath);
        env.insert ("PEONY_ALPHA_WALLPAPER", kMarkerPath);
        env.insert ("PEONY_ALPHA_LOG", "/tmp/shim-hook-ctor.log");
        env.insert ("QT_QPA_PLATFORM", "offscreen");
        probe.setProcessEnvironment (env);
        probe.start (kProbePath, { "ctor", kMarkerPath });
        QVERIFY (probe.waitForStarted (5000));
        QVERIFY (probe.waitForFinished (30000));
        const QString out = QString::fromUtf8 (probe.readAllStandardOutput ());
        QCOMPARE (out.trimmed (), QString ("RESULT transparent=1"));
    }

    void xcbHook_rewritesDesktopType () {
        if (QProcessEnvironment::systemEnvironment ().value ("DISPLAY").isEmpty ())
            QSKIP ("no DISPLAY — the xcb rewrite probe needs an X server");
        QProcess probe;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment ();
        env.insert ("LD_PRELOAD", kShimPath);
        env.insert ("PEONY_ALPHA_WALLPAPER", kMarkerPath);
        probe.setProcessEnvironment (env);
        probe.start (kProbePath, { "xcbtype" });
        QVERIFY (probe.waitForStarted (5000));
        QVERIFY (probe.waitForFinished (30000));
        const QString out = QString::fromUtf8 (probe.readAllStandardOutput ());
        QVERIFY (out.contains ("RESULT type-after=NORMAL"));
    }
};

QTEST_MAIN (ShimHookTest)
#include "shim_hook_test.moc"
