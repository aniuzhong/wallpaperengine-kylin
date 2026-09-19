// Integration tests for SystemdUnit: real systemd user manager on the
// session bus. Skips automatically when no user bus is available (e.g. CI
// without enable-linger). Every unit created here uses the isolated
// "lwe-test-" prefix and is stopped/removed afterwards.
#include "../src/systemd/systemdunit.h"
#include "../src/systemd/unitbuilder.h"

#include <QDBusConnection>
#include <QDBusVariant>
#include <QProcess>
#include <QDBusMessage>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include <csignal>
#include <fcntl.h>
#include <memory>
#include <unistd.h>


using namespace SystemdLayer;

class SystemdUnitTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase () {
        if (!QDBusConnection::sessionBus ().isConnected ())
            QSKIP ("no session bus — integration tests need a systemd user session");
        // probe the manager; skip when systemd is not reachable on it
        QDBusMessage call = QDBusMessage::createMethodCall (
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.DBus.Properties", "Get");
        call.setArguments ({ "org.freedesktop.systemd1.Manager", "Version" });
        if (QDBusConnection::sessionBus ().call (call).type () == QDBusMessage::ErrorMessage)
            QSKIP ("systemd user manager not reachable on the session bus");
    }

    void init () {
        // fresh random name per test so tests never share state
        m_name = QString ("lwe-test-%1.service")
                     .arg (QRandomGenerator::global ()->bounded (100000, 999999));
        m_unit = std::make_unique<SystemdUnit> (m_name);
    }

    void cleanup () {
        m_unit->stop ();
        m_unit->removeUnitFile ();
        QFile unitFile (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) +
                        "/systemd/user/" + m_name);
        QFile::remove (unitFile.fileName ());
    }

    // ---- pure install behavior --------------------------------------------

    void installUnitFile_createsFileAndLoadsUnit () {
        QVERIFY (m_unit->installUnitFile (sleepUnitContent ()));
        const QString path = QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) +
                             "/systemd/user/" + m_name;
        QVERIFY (QFile::exists (path));

        Error error;
        const QString state = m_unit->activeState (&error);
        QCOMPARE (error.kind, Error::NoError); // unit is loaded once installed
        QVERIFY (state == "inactive" || state == "active" || state == "failed");
    }

    void installUnitFile_textSurvivesRoundtrip () {
        const QString content = sleepUnitContent () + "# marker line\n";
        QVERIFY (m_unit->installUnitFile (content));
        const QString path = QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) +
                             "/systemd/user/" + m_name;
        QFile file (path);
        QVERIFY (file.open (QIODevice::ReadOnly));
        QCOMPARE (QString::fromUtf8 (file.readAll ()), content);
    }

    // ---- lifecycle ---------------------------------------------------------

    void lifecycle_startStopRestart () {
        QVERIFY (m_unit->installUnitFile (sleepUnitContent ()));

        QVERIFY (m_unit->start ());
        QTRY_COMPARE (m_unit->activeState (), QString ("active"));

        QVERIFY (m_unit->stop ());
        QTRY_COMPARE (m_unit->activeState (), QString ("inactive"));

        QVERIFY (m_unit->restart ());
        QTRY_COMPARE (m_unit->activeState (), QString ("active"));

        QVERIFY (m_unit->stop ());
    }

    void selfHeal_onMainProcessKill () {
        UnitDefinition def;
        def.description = "lwe self-heal test";
        def.execArgs = QStringList { QStringLiteral ("/bin/sleep"), QStringLiteral ("3600") };
        def.restartOnFailure = true;
        QVERIFY (m_unit->installUnitFile (buildUnitFile (def)));

        QVERIFY (m_unit->start ());
        QTRY_COMPARE (m_unit->activeState (), QString ("active"));
        const qint64 firstPid = mainPid ();
        QVERIFY (firstPid > 0);

        kill (firstPid, SIGKILL);

        // Restart=on-failure must bring the unit back with a fresh pid
        QTRY_VERIFY_WITH_TIMEOUT (mainPid () > 0 && mainPid () != firstPid && m_unit->isActive (), 15000);
        QVERIFY (m_unit->stop ());
    }

    void stateChanged_signalFiresOnStart () {
        QVERIFY (m_unit->installUnitFile (sleepUnitContent ()));
        QSignalSpy spy (m_unit.get (), &SystemdUnit::stateChanged);
        QVERIFY (spy.isValid ());

        QVERIFY (m_unit->start ());
        // the PropertiesChanged signal arrives asynchronously on the bus
        QTRY_VERIFY_WITH_TIMEOUT (!spy.isEmpty (), 10000);
        QVERIFY (m_unit->stop ());
    }

    // ---- transient units (the peony injection mechanism) -------------------

    void transient_environmentIsInjected () {
        QMap<QString, QString> env;
        env.insert ("LWE_TEST_MARKER", "present");
        QVERIFY (m_unit->startTransient (QStringList { QStringLiteral ("/bin/sleep"), QStringLiteral ("3600") }, env, {}));

        QTRY_COMPARE (m_unit->activeState (), QString ("active"));
        // ActiveState flips at fork; the unit Environment lands on the
        // process at execve microseconds later — poll, don't single-read
        bool envApplied = false;
        qint64 pid = 0;
        for (int waited = 0; waited < 5000 && !envApplied; waited += 200) {
            pid = mainPid ();
            if (pid <= 0) {
                QThread::msleep (200);
                continue;
            }
            // /proc files report size 0: readAll() truncates at the first
            // entry — drain with POSIX reads instead
            const int fd = ::open (QString ("/proc/%1/environ").arg (pid).toUtf8 ().constData (), O_RDONLY);
            if (fd >= 0) {
                QString content;
                char buf[8192];
                ssize_t n;
                while ((n = ::read (fd, buf, sizeof buf)) > 0)
                    content += QString::fromLatin1 (buf, static_cast<int> (n));
                ::close (fd);
                envApplied = content.contains ("LWE_TEST_MARKER");
            }
            if (!envApplied)
                QThread::msleep (200);
        }
        QVERIFY2 (envApplied, "transient Environment never reached the process environ");

        QDBusMessage envGet = QDBusMessage::createMethodCall (
            "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1/unit/" + m_unit->unitName ().replace ('-', "_2d").replace ('.', "_2e"),
            "org.freedesktop.DBus.Properties", "Get");
        envGet.setArguments ({ "org.freedesktop.systemd1.Service", "Environment" });
        const QDBusMessage envReply = QDBusConnection::sessionBus ().call (envGet);
        const QVariant envValue = envReply.arguments ().value (0).value<QDBusVariant> ().variant ();
        qWarning () << "DIAG unit Environment property meta:" << envValue.typeName ()
                    << "content:" << envValue.toStringList ();

        QVERIFY (m_unit->stop ());
    }

    // ---- error paths --------------------------------------------------------

    void error_stopNonexistentUnitIsTyped () {
        SystemdUnit ghost ("lwe-test-nonexistent-does-not-exist.service");
        Error error;
        ghost.stop (&error);
        QVERIFY (error.kind != Error::NoError);
        QVERIFY (!error.dbusName.isEmpty ());
        QVERIFY (!error.message.isEmpty ());
    }

    void transient_restartPreservesEnvironment () {
        // THE injection-persistence guarantee: killing the main process must
        // bring the transient unit back WITH its environment (the peony
        // LD_PRELOAD chain survives crashes by construction)
        QMap<QString, QString> env;
        env.insert ("LWE_TEST_MARKER", "present");
        QVERIFY (m_unit->startTransient (QStringList { QStringLiteral ("/bin/sleep"), QStringLiteral ("3600") }, env, {}));
        QTRY_COMPARE (m_unit->activeState (), QString ("active"));
        const qint64 firstPid = mainPid ();
        QVERIFY (firstPid > 0);

        ::kill (firstPid, SIGKILL);

        bool restarted = false;
        qint64 newPid = 0;
        for (int waited = 0; waited < 10000 && !restarted; waited += 200) {
            newPid = mainPid ();
            restarted = newPid > 0 && newPid != firstPid && m_unit->isActive ();
            if (!restarted)
                QThread::msleep (200);
        }
        QVERIFY2 (restarted, "transient unit did not auto-restart after SIGKILL");

        // /proc reads truncate via QFile::readAll — drain with POSIX reads
        QFile environ (QString ("/proc/%1/environ").arg (newPid));
        QVERIFY (environ.open (QIODevice::ReadOnly));
        QString envContent;
        {
            char buf[8192];
            ssize_t n;
            while ((n = ::read (environ.handle (), buf, sizeof buf)) > 0)
                envContent += QString::fromLatin1 (buf, static_cast<int> (n));
        }
        if (!envContent.contains ("LWE_TEST_MARKER")) {
            QProcess ps;
            ps.start ("ps", { "-o", "args=", "-p", QString::number (newPid) });
            ps.waitForFinished (2000);
            qWarning () << "DIAG restarted pid" << newPid << "args:"
                        << QString::fromUtf8 (ps.readAllStandardOutput ())
                        << "environ:" << envContent.left (200);
        }
        QVERIFY (envContent.contains ("LWE_TEST_MARKER"));

        QVERIFY (m_unit->stop ());
    }

    void resetFailed_onUnknownUnitIsHarmless () {
        QVERIFY (m_unit->resetFailed ());
    }

    void transient_rejectsEmptyArgv () {
        Error error;
        QVERIFY (!m_unit->startTransient ({}, {}, {}, &error));
        QCOMPARE (error.kind, Error::InvalidInput);
    }

    // ---- helpers -------------------------------------------------------------

private:
    static QString sleepUnitContent () {
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

    qint64 mainPid () const {
        QDBusMessage call = QDBusMessage::createMethodCall (
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1/unit/" + m_unit->unitName ().replace ('-', "_2d")
                                            .replace ('.', "_2e"),
            "org.freedesktop.DBus.Properties", "Get");
        call.setArguments ({ "org.freedesktop.systemd1.Service", "ExecMainPID" });
        const QDBusMessage reply = QDBusConnection::sessionBus ().call (call);
        if (reply.type () == QDBusMessage::ErrorMessage || reply.arguments ().isEmpty ())
            return 0;
        return reply.arguments ().first ().value<QDBusVariant> ().variant ().toLongLong ();
    }

    QString m_name;
    std::unique_ptr<SystemdUnit> m_unit;
};

QTEST_MAIN (SystemdUnitTest)
#include "systemdunit_test.moc"
