// Pure-logic tests for the systemd layer: ExecStart escaping/parsing and
// transient property decomposition. No bus required — these must pass
// everywhere.
#include "../src/unitbuilder.h"

#include <QtTest>

#include <algorithm>

using namespace SystemdLayer;

namespace {
std::vector<std::string> joinArgs (const std::vector<std::string>& args) {
    // join the escaped forms back into one ExecStart line
    std::string line;
    for (const std::string& arg : args) {
        if (!line.empty ())
            line += ' ';
        line += arg;
    }
    return parseExecArgs (line);
}
} // namespace

class UnitBuilderTest : public QObject {
    Q_OBJECT

private slots:
    void escapeExecArg_plainArgStaysUntouched () {
        QCOMPARE (escapeExecArg ("/usr/bin/sleep"), std::string ("/usr/bin/sleep"));
        QCOMPARE (escapeExecArg ("3600"), std::string ("3600"));
    }

    void escapeExecArg_spaceIsQuoted () {
        QCOMPARE (escapeExecArg ("hello world"), std::string ("\"hello world\""));
    }

    void escapeExecArg_quoteIsEscaped () {
        QCOMPARE (escapeExecArg ("say \"hi\""), std::string ("\"say \\\"hi\\\"\""));
    }

    void escapeExecArg_dollarAndPercentDoubled () {
        // systemd substitutes $VAR and %specifiers in ExecStart; literals
        // must be doubled
        QCOMPARE (escapeExecArg ("$HOME"), std::string ("$$HOME"));
        QCOMPARE (escapeExecArg ("%h"), std::string ("%%h"));
    }

    void escapeExecArg_semicolonIsQuoted () {
        // ';' separates commands inside a single ExecStart line
        QCOMPARE (escapeExecArg ("a;b"), std::string ("\"a;b\""));
    }

    void toExecCommand_decomposesArgv () {
        const ExecCommand command = toExecCommand (
            std::vector<std::string> { "/usr/bin/tool", "--flag", "x" });
        QCOMPARE (command.program, std::string ("/usr/bin/tool"));
        QCOMPARE (command.args.size (), size_t (3));
        QCOMPARE (command.args.front (), std::string ("/usr/bin/tool"));
    }

    void toExecCommand_emptyArgvGivesNullCommand () {
        const ExecCommand command = toExecCommand ({});
        QVERIFY (command.program.empty ());
        QVERIFY (command.args.empty ());
    }

    void parseExecArgs_plainLine () {
        const std::vector<std::string> expected { "/bin/sleep", "3600" };
        QCOMPARE (parseExecArgs ("/bin/sleep 3600"), expected);
    }

    void parseExecArgs_invertsEscapeExecArg () {
        // the roundtrip the unit file depends on: unitFileContent escapes,
        // unitBackgrounds parses — any argv must survive unchanged
        const std::vector<std::string> argv { "/usr/bin/engine",
                                              "--bg",
                                              "843532366",
                                              "some dir/file",
                                              "say \"hi\"",
                                              "a;b",
                                              "a\\b",
                                              "tab\tinside" };
        std::string line;
        for (const std::string& arg : argv) {
            if (!line.empty ())
                line += ' ';
            line += escapeExecArg (arg);
        }
        QCOMPARE (parseExecArgs (line), argv);
    }

    void parseExecArgs_undoublesDollarAndPercent () {
        // systemd undoes the doubling before exec, so the parsed argv must
        // show the literal characters the engine will see
        QCOMPARE (parseExecArgs ("$$HOME"), std::vector<std::string> { "$HOME" });
        QCOMPARE (parseExecArgs ("%%h"), std::vector<std::string> { "%h" });
        QCOMPARE (parseExecArgs ("\"/tmp/a$$b c\""), std::vector<std::string> { "/tmp/a$b c" });
    }
};

QTEST_MAIN (UnitBuilderTest)
#include "unitbuilder_test.moc"
