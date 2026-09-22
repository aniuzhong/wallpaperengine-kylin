// integration tests for the systemd module: real systemd user manager on
// the session bus. Skips automatically when no user bus is available (e.g.
// CI without enable-linger). Every unit created here uses the isolated
// "wallpaper-engine-test-" prefix and is stopped/removed afterwards.
#include "../src/systemd_unit.h"

#include <QDBusConnection>
#include <QDBusVariant>
#include <QDir>
#include <QProcess>
#include <QDBusMessage>
#include <QFile>
#include <QStandardPaths>
#include <QtTest>

#include <csignal>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <unistd.h>


using namespace systemd;

namespace {
// the unit object path the manager derives from the id ('-' -> _2d, '.' -> _2e)
QString unitPathFor(const std::string& UnitName) {
    return QString::fromStdString(UnitName).replace('-', "_2d").replace('.', "_2e");
}
} // namespace

class SystemdUnitTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("no session bus — integration tests need a systemd user session");
        // probe the manager; skip when systemd is not reachable on it
        QDBusMessage call = QDBusMessage::createMethodCall(
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.DBus.Properties", "Get");
        call.setArguments({ "org.freedesktop.systemd1.Manager", "Version" });
        if (QDBusConnection::sessionBus().call(call).type() == QDBusMessage::ErrorMessage)
            QSKIP("systemd user manager not reachable on the session bus");

        auto bus = Connection::UserBus();
        if (!bus)
            QSKIP("sd-bus cannot open the user bus");
        bus_.emplace(std::move(bus).value());
    }

    void init() {
        // fresh random name per test so tests never share state
        m_name = "wallpaper-engine-test-" + std::to_string(QRandomGenerator::global()->bounded(100000, 999999)) + ".service";
    }

    void cleanup() {
        if (bus_)
            (void)systemd::Stop(*bus_, m_name);
        QFile unitFile(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                        "/systemd/user/" + QString::fromStdString(m_name));
        QFile::remove(unitFile.fileName());
    }

    // ---- pure install behavior --------------------------------------------

    void installUnitFile_createsFileAndLoadsUnit() {
        QVERIFY(installUnitFile(sleepUnitContent()));
        const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                             "/systemd/user/" + QString::fromStdString(m_name);
        QVERIFY(QFile::exists(path));

        // installed ⇒ loaded: a real state comes back, not the not-loaded
        // nullopt
        auto state = systemd::ActiveState(*bus_, m_name);
        QVERIFY(state.has_value());
        QVERIFY(state->has_value());
        QVERIFY(**state == UnitState::Inactive || **state == UnitState::Active || **state == UnitState::Failed);
    }

    void installUnitFile_textSurvivesRoundtrip() {
        const std::string content = sleepUnitContent() + "# marker line\n";
        QVERIFY(installUnitFile(content));
        const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                             "/systemd/user/" + QString::fromStdString(m_name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), QString::fromStdString(content));
    }

    // ---- lifecycle ---------------------------------------------------------

    void lifecycle_startStopRestart() {
        QVERIFY(installUnitFile(sleepUnitContent()));

        QVERIFY(systemd::Start(*bus_, m_name).has_value());
        QTRY_VERIFY(stateNow() == UnitState::Active);

        QVERIFY(systemd::Stop(*bus_, m_name).has_value());
        QTRY_VERIFY(stateNow() == UnitState::Inactive);

        QVERIFY(systemd::Restart(*bus_, m_name).has_value());
        QTRY_VERIFY(stateNow() == UnitState::Active);

        QVERIFY(systemd::Stop(*bus_, m_name).has_value());
    }

    void selfHeal_onMainProcessKill() {
        QVERIFY(installUnitFile(selfHealUnitContent()));

        QVERIFY(systemd::Start(*bus_, m_name).has_value());
        QTRY_VERIFY(stateNow() == UnitState::Active);
        const qint64 firstPid = mainPid();
        QVERIFY(firstPid > 0);

        kill(firstPid, SIGKILL);

        // Restart=on-failure must bring the unit back with a fresh pid
        QTRY_VERIFY_WITH_TIMEOUT(mainPid() > 0 && mainPid() != firstPid && stateNow() == UnitState::Active, 15000);
        QVERIFY(systemd::Stop(*bus_, m_name).has_value());
    }

    // ---- transient units (the peony injection mechanism) -------------------

    void transient_environmentIsInjected() {
        TransientSpec spec;
        spec.unit = m_name;
        spec.argv = { "/bin/sleep", "3600" };
        spec.environment = { { "LWE_TEST_MARKER", "present" } };
        QVERIFY(systemd::StartTransient(*bus_, spec).has_value());

        QTRY_VERIFY(stateNow() == UnitState::Active);
        // ActiveState flips at fork; the unit Environment lands on the
        // process at execve microseconds later — poll, don't single-read
        bool envApplied = false;
        qint64 pid = 0;
        for (int waited = 0; waited < 5000 && !envApplied; waited += 200) {
            pid = mainPid();
            if (pid <= 0) {
                QThread::msleep(200);
                continue;
            }
            // /proc files report size 0: readAll() truncates at the first
            // entry — drain with POSIX reads instead
            const int fd = ::open(QString("/proc/%1/environ").arg(pid).toUtf8().constData(), O_RDONLY);
            if (fd >= 0) {
                QString content;
                char buf[8192];
                ssize_t n;
                while ((n = ::read(fd, buf, sizeof buf)) > 0)
                    content += QString::fromLatin1(buf, static_cast<int> (n));
                ::close(fd);
                envApplied = content.contains("LWE_TEST_MARKER");
            }
            if (!envApplied)
                QThread::msleep(200);
        }
        QVERIFY2(envApplied, "transient Environment never reached the process environ");

        QDBusMessage envGet = QDBusMessage::createMethodCall(
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1/unit/" + unitPathFor(m_name),
            "org.freedesktop.DBus.Properties", "Get");
        envGet.setArguments({ "org.freedesktop.systemd1.Service", "Environment" });
        const QDBusMessage envReply = QDBusConnection::sessionBus().call(envGet);
        const QVariant envValue = envReply.arguments().value(0).value<QDBusVariant> ().variant();
        qWarning() << "DIAG unit Environment property meta:" << envValue.typeName()
                    << "content:" << envValue.toStringList();

        QVERIFY(systemd::Stop(*bus_, m_name).has_value());
    }

    // ---- error paths --------------------------------------------------------

    void error_stopNonexistentUnitIsTyped() {
        auto result = systemd::Stop(*bus_, "wallpaper-engine-test-nonexistent-does-not-exist.service");
        QVERIFY(!result.has_value());
        const wallpaper_engine::Error error = std::move(result).error();
        QVERIFY(error.kind != wallpaper_engine::Error::NoError);
        QVERIFY(!error.dbusName.empty());
        QVERIFY(!error.message.empty());
        // Tolerated() keys off this exact name — pin it so a systemd
        // wording change cannot silently break the idempotent-stop promise
        QVERIFY(error.dbusName.find("NoSuchUnit") != std::string::npos);
    }

    void transient_restartPreservesEnvironment() {
        // THE injection-persistence guarantee: killing the main process must
        // bring the transient unit back WITH its environment (the peony
        // LD_PRELOAD chain survives crashes by construction)
        TransientSpec spec;
        spec.unit = m_name;
        spec.argv = { "/bin/sleep", "3600" };
        spec.environment = { { "LWE_TEST_MARKER", "present" } };
        QVERIFY(systemd::StartTransient(*bus_, spec).has_value());
        QTRY_VERIFY(stateNow() == UnitState::Active);
        const qint64 firstPid = mainPid();
        QVERIFY(firstPid > 0);

        ::kill(firstPid, SIGKILL);

        bool restarted = false;
        qint64 newPid = 0;
        for (int waited = 0; waited < 10000 && !restarted; waited += 200) {
            newPid = mainPid();
            restarted = newPid > 0 && newPid != firstPid && stateNow() == UnitState::Active;
            if (!restarted)
                QThread::msleep(200);
        }
        QVERIFY2(restarted, "transient unit did not auto-restart after SIGKILL");

        // /proc reads truncate via QFile::readAll — drain with POSIX reads
        QFile environ(QString("/proc/%1/environ").arg(newPid));
        QVERIFY(environ.open(QIODevice::ReadOnly));
        QString envContent;
        {
            char buf[8192];
            ssize_t n;
            while ((n = ::read(environ.handle(), buf, sizeof buf)) > 0)
                envContent += QString::fromLatin1(buf, static_cast<int> (n));
        }
        if (!envContent.contains("LWE_TEST_MARKER")) {
            QProcess ps;
            ps.start("ps", { "-o", "args=", "-p", QString::number(newPid) });
            ps.waitForFinished(2000);
            qWarning() << "DIAG restarted pid" << newPid << "args:"
                        << QString::fromUtf8(ps.readAllStandardOutput())
                        << "environ:" << envContent.left(200);
        }
        QVERIFY(envContent.contains("LWE_TEST_MARKER"));

        QVERIFY(systemd::Stop(*bus_, m_name).has_value());
    }

    void resetFailed_onUnknownUnitIsHarmless() {
        QVERIFY(systemd::ResetFailed(*bus_, m_name).has_value());
    }

    void transient_rejectsEmptyArgv() {
        TransientSpec spec;
        spec.unit = m_name;
        auto result = systemd::StartTransient(*bus_, spec);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error().kind, wallpaper_engine::Error::InvalidInput);
    }

    // ---- helpers -------------------------------------------------------------

private:
    // the unit's state as the tests compare it: Unknown covers both a failed
    // bus round trip and a unit that is not loaded
    UnitState stateNow() {
        const auto state = systemd::ActiveState(*bus_, m_name);
        return (state && state->has_value()) ? **state : UnitState::Unknown;
    }

    // test-local stand-in for a unit-file installer: write the unit file
    // where the user manager looks and reload it
    bool installUnitFile(const std::string& content) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                            "/systemd/user";
        QDir().mkpath(dir);
        QFile file(dir + "/" + QString::fromStdString(m_name));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        file.write(content.c_str(), qint64(content.size()));
        file.close();
        return systemd::DaemonReload(*bus_).has_value();
    }

    static std::string selfHealUnitContent() {
        return "[Unit]\n"
               "Description=lwe self-heal test\n"
               "\n"
               "[Service]\n"
               "Type=simple\n"
               "Restart=on-failure\n"
               "RestartSec=3\n"
               "ExecStart=/bin/sleep 3600\n"
               "\n"
               "[Install]\n"
               "WantedBy=graphical-session.target\n";
    }

    static std::string sleepUnitContent() {
        return "[Unit]\n"
               "Description=lwe integration test\n"
               "\n"
               "[Service]\n"
               "Type=simple\n"
               "ExecStart=/bin/sleep 3600\n"
               "\n"
               "[Install]\n"
               "WantedBy=graphical-session.target\n";
    }

    qint64 mainPid() const {
        QDBusMessage call = QDBusMessage::createMethodCall(
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1/unit/" + unitPathFor(m_name),
            "org.freedesktop.DBus.Properties", "Get");
        call.setArguments({ "org.freedesktop.systemd1.Service", "ExecMainPID" });
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
            return 0;
        return reply.arguments().first().value<QDBusVariant> ().variant().toLongLong();
    }

    std::string m_name;
    std::optional<Connection> bus_;
};

QTEST_MAIN(SystemdUnitTest)
#include "systemd_unit_test.moc"
