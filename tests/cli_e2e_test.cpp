// T3 end-to-end test of the headless CLI against the real systemd user
// manager, on a dedicated test unit (wallpaper-engine-e2e-test). The engine
// and the workshop library are stubs in a temporary directory, so the
// lifecycle assertions are hermetic: they need a session bus and nothing
// else. The user's own config file is backed up wholesale and restored.
#include "../src/config.h"
#include "../src/paths.h"
#include "../src/engine_unit.h"

#include <QDBusConnection>
#include <QDir>
#include <QFileDevice>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#ifndef WALLPAPER_ENGINE_BIN
#define WALLPAPER_ENGINE_BIN "/usr/bin/true"
#endif

namespace {
constexpr const char* kTestUnit = "wallpaper-engine-e2e-test";
constexpr const char* kTestWallpaper = "843532366";

QString UnitState(const QString& unit) {
    QProcess process;
    process.start("systemctl", QStringList { "--user", "is-active", unit });
    process.waitForFinished(10000);
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

struct CliResult {
    int exitCode = 1;
    QByteArray stdoutBytes;
    QByteArray stderrBytes;
};

CliResult RunCli(const QStringList& args) {
    QProcess cli;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("WALLPAPER_ENGINE_UNIT", kTestUnit);
    cli.setProcessEnvironment(env);
    cli.start(WALLPAPER_ENGINE_BIN, args);
    if (!cli.waitForStarted(5000)) {
        qWarning() << "CLI did not start:" << cli.errorString();
        return { 1, {}, cli.errorString().toUtf8() };
    }
    if (!cli.waitForFinished(60000)) {
        cli.kill();
        return { 1, {}, "timeout" };
    }
    return { cli.exitCode(), cli.readAllStandardOutput(), cli.readAllStandardError() };
}

} // namespace

class CliE2eTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("no session bus — e2e needs a systemd user session");

        // back up the user's config wholesale; cleanupTestCase puts the
        // exact bytes back (or removes the file when there was none)
        const QString configPath = QString::fromStdString(we::paths::ConfigFile());
        QFile configFile(configPath);
        m_configExisted = configFile.exists();
        if (m_configExisted && configFile.open(QIODevice::ReadOnly))
            m_configBackup = configFile.readAll();

        // the same fallback the CLI itself uses: both this test process and
        // the CLI subprocess are headless, so they resolve identically
        m_screen = QString::fromStdString(engine_unit::FallbackScreenName());

        // the test process itself must address the SAME unit as the CLI
        // subprocesses it spawns
        qputenv("WALLPAPER_ENGINE_UNIT", kTestUnit);

        // the stub engine: ignores its arguments, stays alive — the unit
        // under test reaches "active" without a real engine install
        QVERIFY(m_tempDir.isValid());
        const QString stub = m_tempDir.filePath("engine-stub.sh");
        {
            QFile script(stub);
            QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
            script.write("#!/bin/sh\nexec sleep 86400\n");
        }
        QFile::setPermissions(stub, QFile::permissions(stub) | QFileDevice::ExeOwner | QFileDevice::ExeGroup |
                                         QFileDevice::ExeOther);

        // the stub workshop library: one wallpaper, so `switch` accepts the
        // id without a Steam install
        const std::string workshop = m_tempDir.path().toStdString() + "/workshop";
        QDir().mkpath(QString::fromStdString(workshop + "/" + kTestWallpaper));
        {
            QFile project(QString::fromStdString(workshop + "/" + kTestWallpaper + "/project.json"));
            QVERIFY(project.open(QIODevice::WriteOnly | QIODevice::Truncate));
            project.write("{\"title\": \"e2e stub\", \"type\": \"scene\"}\n");
        }

        config::Config config = config::Config::Load();
        config.enginePath = stub.toStdString();
        config.workshopDir = workshop;
        config.screens.clear();
        QVERIFY(config.Save());

        QProcess::execute("systemctl", QStringList { "--user", "stop", kTestUnit });
        QProcess::execute("systemctl", QStringList { "--user", "reset-failed", kTestUnit });
    }

    // one continuous flow: the unit must stay up across CLI invocations —
    // a per-test cleanup stop here would break the following assertions
    void cli_lifecycle_end_to_end() {
        const CliResult switched = RunCli({ "switch", kTestWallpaper });
        if (switched.exitCode != 0)
            qWarning() << "switch failed:" << switched.stdoutBytes << switched.stderrBytes;
        QCOMPARE(switched.exitCode, 0);

        // unit file updated, loaded by the manager and running
        QFile unit(QString::fromStdString(engine_unit::UnitPath()));
        QVERIFY(unit.exists());
        QVERIFY(unit.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(unit.readAll()).contains("--bg " + QString(kTestWallpaper)));
        QTRY_COMPARE(UnitState(kTestUnit), QString("active"));

        // config.json persisted the switch
        const config::Config afterSwitch = config::Config::Load();
        const auto selected = afterSwitch.screens.find(m_screen.toStdString());
        QVERIFY(selected != afterSwitch.screens.end());
        QCOMPARE(QString::fromStdString(selected->second), QString(kTestWallpaper));

        // status --json: structure assertions only — the transient
        // active/inactive state is covered by the pause/resume cycle below
        // (the engine pauses itself on fullscreen, which makes a strict
        // state assert racy)
        const CliResult statusOut = RunCli({ "status", "--json" });
        QCOMPARE(statusOut.exitCode, 0);
        const QJsonObject status =
            QJsonDocument::fromJson(statusOut.stdoutBytes).object().value("status").toObject();
        QCOMPARE(status.value("unit").toString(), QString(kTestUnit));
        QVERIFY(status.value("state").isString());
        QCOMPARE(status.value("screens").toObject().value(m_screen).toString(), kTestWallpaper);

        // pause/resume cycle: the unit comes back with the SAME wallpaper
        QVERIFY(RunCli({ "pause" }).exitCode == 0);
        QTRY_COMPARE(UnitState(kTestUnit), QString("inactive"));
        QVERIFY(RunCli({ "resume" }).exitCode == 0);
        QTRY_COMPARE(UnitState(kTestUnit), QString("active"));
        QFile unitAfterResume(QString::fromStdString(engine_unit::UnitPath()));
        QVERIFY(unitAfterResume.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(unitAfterResume.readAll()).contains("--bg " + QString(kTestWallpaper)));
    }

    void cleanup() {
        QProcess::execute("systemctl", QStringList { "--user", "stop", kTestUnit });
        QProcess::execute("systemctl", QStringList { "--user", "reset-failed", kTestUnit });
    }

    void cleanupTestCase() {
        // put the user's config back exactly as it was
        const QString configPath = QString::fromStdString(we::paths::ConfigFile());
        if (!m_configExisted) {
            QFile::remove(configPath);
            return;
        }
        QFile configFile(configPath);
        if (configFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            configFile.write(m_configBackup);
    }

private:
    QTemporaryDir m_tempDir;
    QByteArray m_configBackup;
    bool m_configExisted = false;
    QString m_screen;
};

// QTEST_GUILESS_MAIN: the headless CLI control plane needs no platform
// plugin — a QGuiApplication (QTEST_MAIN, Gui is linked for QScreen) cannot
// initialize in the container test stage where no display exists.
QTEST_GUILESS_MAIN(CliE2eTest)
#include "cli_e2e_test.moc"
