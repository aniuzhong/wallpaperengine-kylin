// T3 end-to-end test of the headless CLI against the real systemd user
// manager. Uses the real config but a dedicated test unit name
// (linux-wallpaperengine-e2e-test) and restores the previous wallpaper selection afterwards,
// so the user's desktop state is preserved.
#include "../src/service/config.h"
#include "../src/service/engineunit.h"

#include <QDBusConnection>
#include <QProcess>
#include <QtTest>

#ifndef WALLPAPER_ENGINE_BIN
#define WALLPAPER_ENGINE_BIN "/usr/bin/true"
#endif

namespace {
constexpr const char* kTestUnit = "linux-wallpaperengine-e2e-test";

QString unitState (const QString& unit) {
    QProcess process;
    process.start ("systemctl", QStringList { "--user", "is-active", unit });
    process.waitForFinished (10000);
    return QString::fromUtf8 (process.readAllStandardOutput ()).trimmed ();
}

struct CliResult {
    int exitCode = 1;
    QByteArray stdoutBytes;
    QByteArray stderrBytes;
};

CliResult runCli (const QStringList& args) {
    QProcess cli;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment ();
    env.insert ("WALLPAPER_ENGINE_UNIT", kTestUnit);
    cli.setProcessEnvironment (env);
    cli.start (WALLPAPER_ENGINE_BIN, args);
    if (!cli.waitForStarted (5000)) {
        qWarning () << "CLI did not start:" << cli.errorString ();
        return { 1, {}, cli.errorString ().toUtf8 () };
    }
    if (!cli.waitForFinished (60000)) {
        cli.kill ();
        return { 1, {}, "timeout" };
    }
    return { cli.exitCode (), cli.readAllStandardOutput (), cli.readAllStandardError () };
}

} // namespace

class CliE2eTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase () {
        if (!QDBusConnection::sessionBus ().isConnected ())
            QSKIP ("no session bus — e2e needs a systemd user session");

        // the wallpaper under test: the one already selected whenever
        // possible, so the visual state does not even change
        Config config = Config::load ();
        m_previous = config.screens.empty () ? QString () : QString::fromStdString (config.screens.begin ()->second);
        m_target = m_previous.isEmpty () ? QString ("843532366") : m_previous;
        // the same fallback the CLI itself uses: both this test process and
        // the CLI subprocess are headless, so they resolve identically
        m_screen = QString::fromStdString (EngineUnit::fallbackScreenName ());

        // the test process itself must address the SAME unit as the CLI
        // subprocesses it spawns
        qputenv ("WALLPAPER_ENGINE_UNIT", kTestUnit);

        QProcess::execute ("systemctl", QStringList { "--user", "stop", kTestUnit });
        QProcess::execute ("systemctl", QStringList { "--user", "reset-failed", kTestUnit });
    }

    // one continuous flow: the unit must stay up across CLI invocations —
    // a per-test cleanup stop here would break the following assertions
    void cli_lifecycle_end_to_end () {
        const CliResult switched = runCli ({ "switch", m_target });
        QCOMPARE (switched.exitCode, 0);

        // unit file updated, loaded by the manager and running
        QFile unit (QString::fromStdString (EngineUnit::unitPath ()));
        QVERIFY (unit.exists ());
        QVERIFY (unit.open (QIODevice::ReadOnly));
        QVERIFY (QString::fromUtf8 (unit.readAll ()).contains ("--bg " + m_target));
        QCOMPARE (unitState (kTestUnit), QString ("active"));

        // config.json persisted the switch
        const Config afterSwitch = Config::load ();
        const auto selected = afterSwitch.screens.find (m_screen.toStdString ());
        QVERIFY (selected != afterSwitch.screens.end ());
        QCOMPARE (QString::fromStdString (selected->second), m_target);

        // status --json: structure assertions only — the transient
        // active/inactive state is covered by the pause/resume cycle below
        // (the engine pauses itself on fullscreen, which makes a strict
        // state assert racy)
        const CliResult statusOut = runCli ({ "status", "--json" });
        QCOMPARE (statusOut.exitCode, 0);
        const QJsonObject status =
            QJsonDocument::fromJson (statusOut.stdoutBytes).object ().value ("status").toObject ();
        QCOMPARE (status.value ("unit").toString (), QString ("linux-wallpaperengine-e2e-test"));
        QVERIFY (status.value ("state").isString ());
        QCOMPARE (status.value ("screens").toObject ().value (m_screen).toString (), m_target);

        // pause/resume cycle: the unit comes back with the SAME wallpaper
        QVERIFY (runCli ({ "pause" }).exitCode == 0);
        QCOMPARE (unitState (kTestUnit), QString ("inactive"));
        QVERIFY (runCli ({ "resume" }).exitCode == 0);
        QTRY_COMPARE (unitState (kTestUnit), QString ("active"));
        QFile unitAfterResume (QString::fromStdString (EngineUnit::unitPath ()));
        QVERIFY (unitAfterResume.open (QIODevice::ReadOnly));
        QVERIFY (QString::fromUtf8 (unitAfterResume.readAll ()).contains ("--bg " + m_target));
    }

    void cleanup () {
        // restore the user's previous selection and stop the test unit
        Config config = Config::load ();
        if (!m_previous.isEmpty ()) {
            config.screens.clear ();
            config.screens[m_screen.toStdString ()] = m_previous.toStdString ();
            config.save ();
        }
        QProcess::execute ("systemctl", QStringList { "--user", "stop", kTestUnit });
        QProcess::execute ("systemctl", QStringList { "--user", "reset-failed", kTestUnit });
    }

private:
    QString m_previous;
    QString m_target;
    QString m_screen;
};

// QTEST_GUILESS_MAIN: the headless CLI control plane needs no platform
// plugin — a QGuiApplication (QTEST_MAIN, Gui is linked for QScreen) cannot
// initialize in the container test stage where no display exists.
QTEST_GUILESS_MAIN (CliE2eTest)
#include "cli_e2e_test.moc"
