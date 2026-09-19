// Pure-logic tests for the systemd layer: ExecStart escaping/parsing and
// transient property decomposition. No bus required — these must pass
// everywhere.
#include "../src/service/systemd/unitbuilder.h"

#include <QtTest>

using namespace SystemdLayer;

class UnitBuilderTest : public QObject {
    Q_OBJECT

private slots:
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

    void parseExecArgs_plainLine () {
        // no braced init with commas inside the macro: the preprocessor
        // would split it as extra arguments
        const QStringList expected { QStringLiteral ("/bin/sleep"), QStringLiteral ("3600") };
        QCOMPARE (parseExecArgs (QString ("/bin/sleep 3600")), expected);
    }

    void parseExecArgs_invertsEscapeExecArg () {
        // the roundtrip the unit file depends on: unitFileContent escapes,
        // unitBackgrounds parses — any argv must survive unchanged
        const QStringList argv { QStringLiteral ("/usr/bin/engine"),
                                 QStringLiteral ("--bg"),
                                 QStringLiteral ("843532366"),
                                 QStringLiteral ("some dir/file"),
                                 QStringLiteral ("say \"hi\""),
                                 QStringLiteral ("a;b"),
                                 QStringLiteral ("a\\b"),
                                 QStringLiteral ("tab\tinside") };
        QStringList line;
        for (const QString& arg : argv)
            line << escapeExecArg (arg);
        QCOMPARE (parseExecArgs (line.join (' ')), argv);
    }

    void parseExecArgs_undoublesDollarAndPercent () {
        // systemd undoes the doubling before exec, so the parsed argv must
        // show the literal characters the engine will see
        QCOMPARE (parseExecArgs (QString ("$$HOME")), QStringList { "$HOME" });
        QCOMPARE (parseExecArgs (QString ("%%h")), QStringList { "%h" });
        QCOMPARE (parseExecArgs (QString ("\"/tmp/a$$b c\"")), QStringList { "/tmp/a$b c" });
    }
};

QTEST_MAIN (UnitBuilderTest)
#include "unitbuilder_test.moc"
