// Pure-logic tests for the systemd layer: unit file text and transient
// property decomposition. No bus required — these must pass everywhere.
#include "../src/systemd/unitbuilder.h"

#include <QtTest>

using namespace SystemdLayer;

class UnitBuilderTest : public QObject {
    Q_OBJECT

private slots:
    void unitFile_minimalConfiguration () {
        UnitDefinition def;
        def.description = "test unit";
        def.execArgs = QStringList { QStringLiteral ("/usr/bin/sleep"), QStringLiteral ("3600") };
        def.restartOnFailure = false;
        def.partOf = ""; // no PartOf line when empty

        const QString text = buildUnitFile (def);

        QVERIFY (text.contains ("[Unit]\nDescription=test unit\n"));
        QVERIFY (!text.contains ("PartOf="));
        QVERIFY (text.contains ("[Service]\nType=simple\n"));
        QVERIFY (text.contains ("ExecStart=/usr/bin/sleep 3600\n"));
        QVERIFY (!text.contains ("Environment="));
        QVERIFY (!text.contains ("Restart="));
        QVERIFY (text.endsWith ("\n[Install]\nWantedBy=graphical-session.target\n"));
    }

    void unitFile_environmentAndRestart () {
        UnitDefinition def;
        def.description = "x";
        def.execArgs = QStringList { QStringLiteral ("/bin/a") };
        def.environment = { { "FOO", "bar" }, { "B", "2" } };
        def.restartOnFailure = true;

        const QString text = buildUnitFile (def);
        QVERIFY (text.contains ("Environment="));
        QVERIFY (text.contains ("Restart=on-failure\nRestartSec=3\n"));
    }

    void escapeExecArg_plainArgStaysUntouched () {
        QCOMPARE (escapeExecArg (QString ("/usr/bin/sleep")), QString ("/usr/bin/sleep"));
        QCOMPARE (escapeExecArg (QString ("3600")), QString ("3600"));
    }

    void escapeExecArg_spaceIsQuoted () {
        QCOMPARE (escapeExecArg (QString ("hello world")), QString ("\"hello world\""));
    }

    void escapeExecArg_quoteIsEscaped () {
        QCOMPARE (escapeExecArg (QString ("say \"hi\"")), QString ("\"say \\\"hi\\\"\""));
    }

    void escapeExecArg_dollarAndPercentDoubled () {
        // systemd substitutes $VAR and %specifiers in ExecStart; literals
        // must be doubled
        QCOMPARE (escapeExecArg (QString ("$HOME")), QString ("$$HOME"));
        QCOMPARE (escapeExecArg (QString ("%h")), QString ("%%h"));
    }

    void escapeExecArg_semicolonIsQuoted () {
        // ';' separates commands inside a single ExecStart line
        QCOMPARE (escapeExecArg (QString ("a;b")), QString ("\"a;b\""));
    }

    void toExecCommand_decomposesArgv () {
        const ExecCommand command = toExecCommand (
            QStringList { QStringLiteral ("/usr/bin/tool"), QStringLiteral ("--flag"), QStringLiteral ("x") });
        QCOMPARE (command.program, QString ("/usr/bin/tool"));
        QCOMPARE (command.args.size (), 3);
        QCOMPARE (command.args.first (), QString ("/usr/bin/tool"));
    }

    void toExecCommand_emptyArgvGivesNullCommand () {
        const ExecCommand command = toExecCommand ({});
        QVERIFY (command.program.isEmpty ());
        QVERIFY (command.args.isEmpty ());
    }

    void environmentAssignments_joinsKeyAndValue () {
        QMap<QString, QString> env;
        env.insert ("A", "1");
        env.insert ("LONG", "x y");
        const QStringList assignments = environmentAssignments (env);
        QCOMPARE (assignments.size (), 2);
        QVERIFY (assignments.contains ("A=1"));
        QVERIFY (assignments.contains ("LONG=x y"));
    }

    void buildUnitFile_isDeterministic () {
        UnitDefinition def;
        def.description = "d";
        def.execArgs = QStringList { QStringLiteral ("/bin/a"), QStringLiteral ("b c") };
        QCOMPARE (buildUnitFile (def), buildUnitFile (def));
    }
};

QTEST_MAIN (UnitBuilderTest)
#include "unitbuilder_test.moc"
