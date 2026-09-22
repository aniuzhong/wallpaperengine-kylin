// T0 pure-logic tests for the engine CLI model: contract table integrity,
// grammar-faithful emission, an engine-faithful read-back that replays the
// engine's argparse binding rules over Emit's output, Validate against the
// engine's constraints, and the drift sentinel that reconciles the contract
// against the engine's own ApplicationContext.cpp whenever its source is
// present.
#include "../src/engine.h"

#include <QtTest>

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<std::string>& argv, const std::string& arg) {
    return std::find(argv.begin(), argv.end(), arg) != argv.end();
}

engine::Invocation fullInvocation() {
    engine::Invocation invocation;
    invocation.assetsDir = "/opt/assets";
    engine::ScreenBinding primary;
    primary.screen = "DP-0";
    primary.background = "843532366";
    primary.scaling = "fill";
    primary.clamp = "border";
    engine::ScreenBinding secondary;
    secondary.screen = "HDMI-1";
    secondary.background = "1108150151";
    invocation.screens = {primary, secondary};
    invocation.fps = 60;
    invocation.fullscreenPause = false;
    invocation.automute = false;
    invocation.audioProcessing = false;
    invocation.volume = 42;
    invocation.silent = false;
    invocation.disableParticles = true;
    invocation.disableMouse = true;
    invocation.disableParallax = true;
    invocation.properties = {{"bloom", "1"}, {"schemecolor", "0.1 0.2 0.3"}};
    return invocation;
}

// A faithful replay of the engine's argparse semantics (ApplicationContext::
// loadSettingsFromArgv): --screen-root switches the binding target, the
// following --bg/--scaling/--clamp attach to it, -v/-s are exclusive, the
// positional id lands in the background slot. Whatever this parser recovers
// from Emit's output must equal the invocation that produced it — this is
// the guard on binding order, the one thing string comparison cannot see.
engine::Invocation readBack(const std::vector<std::string>& args) {
    engine::Invocation parsed;
    std::string lastScreen;

    for (size_t i = 0; i < args.size(); i++) {
        const std::string& arg = args[i];
        auto value = [&]() -> const std::string& { return args[++i]; };

        if (arg == "--assets-dir") {
            parsed.assetsDir = value();
        } else if (arg == "--screen-root") {
            lastScreen = value();
            engine::ScreenBinding binding;
            binding.screen = lastScreen;
            parsed.screens.push_back(binding);
        } else if (arg == "--bg") {
            parsed.screens.back().background = value();
        } else if (arg == "--scaling") {
            parsed.screens.back().scaling = value();
        } else if (arg == "--clamp") {
            parsed.screens.back().clamp = value();
        } else if (arg == "--fps") {
            parsed.fps = std::stoi(value());
        } else if (arg == "--no-fullscreen-pause") {
            parsed.fullscreenPause = false;
        } else if (arg == "--noautomute") {
            parsed.automute = false;
        } else if (arg == "--no-audio-processing") {
            parsed.audioProcessing = false;
        } else if (arg == "--silent") {
            parsed.silent = true;
        } else if (arg == "--volume") {
            parsed.volume = std::stoi(value());
        } else if (arg == "--disable-particles") {
            parsed.disableParticles = true;
        } else if (arg == "--disable-mouse") {
            parsed.disableMouse = true;
        } else if (arg == "--disable-parallax") {
            parsed.disableParallax = true;
        } else if (arg == "--set-property") {
            const std::string& pair = value();
            const size_t equals = pair.find('=');
            parsed.properties.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
        } else if (arg == "--list-properties") {
            parsed.listProperties = true;
        } else if (!arg.empty() && arg[0] != '-') {
            parsed.backgroundId = arg;
        }
    }
    return parsed;
}

void compareInvocations(const engine::Invocation& expected, const engine::Invocation& actual) {
    QCOMPARE(QString::fromStdString(expected.assetsDir), QString::fromStdString(actual.assetsDir));
    QCOMPARE(expected.screens.size(), actual.screens.size());
    for (size_t i = 0; i < expected.screens.size(); i++) {
        const engine::ScreenBinding& e = expected.screens[i];
        const engine::ScreenBinding& a = actual.screens[i];
        QCOMPARE(QString::fromStdString(e.screen), QString::fromStdString(a.screen));
        QCOMPARE(QString::fromStdString(e.background), QString::fromStdString(a.background));
        QCOMPARE(QString::fromStdString(e.scaling), QString::fromStdString(a.scaling));
        QCOMPARE(QString::fromStdString(e.clamp), QString::fromStdString(a.clamp));
    }
    QCOMPARE(expected.fps, actual.fps);
    QCOMPARE(expected.fullscreenPause, actual.fullscreenPause);
    QCOMPARE(expected.automute, actual.automute);
    QCOMPARE(expected.audioProcessing, actual.audioProcessing);
    QCOMPARE(expected.volume, actual.volume);
    QCOMPARE(expected.silent, actual.silent);
    QCOMPARE(expected.disableParticles, actual.disableParticles);
    QCOMPARE(expected.disableMouse, actual.disableMouse);
    QCOMPARE(expected.disableParallax, actual.disableParallax);
    QCOMPARE(expected.properties.size(), actual.properties.size());
    for (size_t i = 0; i < expected.properties.size(); i++) {
        QCOMPARE(QString::fromStdString(expected.properties[i].first),
                 QString::fromStdString(actual.properties[i].first));
        QCOMPARE(QString::fromStdString(expected.properties[i].second),
                 QString::fromStdString(actual.properties[i].second));
    }
    QCOMPARE(expected.listProperties, actual.listProperties);
    QCOMPARE(QString::fromStdString(expected.backgroundId), QString::fromStdString(actual.backgroundId));
}

// every long option the contract knows: tokens of each row's flag that start
// with "--"
std::set<std::string> contractLongFlags() {
    std::set<std::string> flags;
    for (const engine::OptionSpec& option : engine::kOptions) {
        std::string name = option.flag;
        size_t start = 0;
        while (start <= name.size()) {
            const size_t slash = name.find('/', start);
            const std::string token = name.substr(
                start, slash == std::string::npos ? std::string::npos : slash - start);
            if (token.rfind("--", 0) == 0)
                flags.insert(token);
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
    }
    return flags;
}

} // namespace

class EngineTest : public QObject {
    Q_OBJECT

private slots:
    void contractTableIsWellFormed() {
        for (const engine::OptionSpec& option : engine::kOptions) {
            QVERIFY(option.flag != nullptr && option.flag[0] != '\0');
            QVERIFY(option.note != nullptr && option.note[0] != '\0');
            // the one positional row is the documented exception to flags
            QVERIFY2(option.flag[0] == '-' || std::string(option.flag) == "background id",
                     (std::string("row is neither an option nor the positional: ") + option.flag).c_str());
            if (option.choices == nullptr)
                continue;
            // every "|"-separated choice must be non-empty
            const std::string choices = option.choices;
            size_t start = 0;
            while (true) {
                const size_t bar = choices.find('|', start);
                QVERIFY2(bar == std::string::npos || bar > start,
                         (std::string("empty choice in ") + option.flag).c_str());
                if (bar == std::string::npos)
                    break;
                start = bar + 1;
            }
        }
    }

    void findMatchesBothNameForms() {
        const engine::OptionSpec* longForm = engine::Find("--fps");
        const engine::OptionSpec* shortForm = engine::Find("-f");
        QVERIFY(longForm != nullptr);
        QVERIFY(shortForm == longForm);
        QCOMPARE(longForm->arity, engine::Arity::Value);
        QVERIFY(engine::Find("--no-such-flag") == nullptr);
        QVERIFY(engine::Find("--scaling") != nullptr);
        QCOMPARE(std::string(engine::Find("--scaling")->choices), std::string("stretch|fit|fill|default"));
    }

    void goldenEmitOrder() {
        const std::vector<std::string> args = engine::Emit(fullInvocation());
        const std::vector<std::string> expected = {
            "--assets-dir", "/opt/assets",
            "--screen-root", "DP-0", "--bg", "843532366", "--scaling", "fill", "--clamp", "border",
            "--screen-root", "HDMI-1", "--bg", "1108150151",
            "--fps", "60", "--no-fullscreen-pause", "--noautomute", "--no-audio-processing",
            "--volume", "42",
            "--disable-particles", "--disable-mouse", "--disable-parallax",
            "--set-property", "bloom=1", "--set-property", "schemecolor=0.1 0.2 0.3",
        };
        QCOMPARE(args.size(), expected.size());
        for (size_t i = 0; i < expected.size(); i++)
            QCOMPARE(QString::fromStdString(args[i]), QString::fromStdString(expected[i]));
    }

    void emptyScalingAndClampAreOmitted() {
        engine::Invocation invocation;
        engine::ScreenBinding binding;
        binding.screen = "DP-0";
        binding.background = "843532366";
        invocation.screens = {binding};
        invocation.fps = 30;
        invocation.silent = true;
        const std::vector<std::string> args = engine::Emit(invocation);
        // the group is exactly the four binding tokens, nothing inherited:
        // group(4) + fps(2) + silent(1)
        const size_t root = std::find(args.begin(), args.end(), std::string("--screen-root")) - args.begin();
        QCOMPARE(args.size() - root, size_t(7));
        QVERIFY(!contains(args, "--scaling"));
        QVERIFY(!contains(args, "--clamp"));
        QVERIFY(!contains(args, "--volume"));
    }

    void emittedFlagsAreContractFlags() {
        const std::vector<std::string> args = engine::Emit(fullInvocation());
        for (const std::string& arg : args) {
            if (arg.rfind("--", 0) != 0)
                continue;
            QVERIFY2(engine::Find(arg) != nullptr,
                     (std::string("emitted flag not in contract: ") + arg).c_str());
        }
    }

    void readBackRecoversTheInvocation() {
        engine::Invocation invocation = fullInvocation();
        invocation.backgroundId = "2317494988";
        invocation.listProperties = true;
        compareInvocations(invocation, readBack(engine::Emit(invocation)));
    }

    void silentWinsOverVolumeInReadBack() {
        engine::Invocation invocation = fullInvocation();
        invocation.silent = true;
        invocation.volume = 42; // inert under --silent: the engine never sees it
        const std::vector<std::string> args = engine::Emit(invocation);
        QVERIFY(contains(args, "--silent"));
        QVERIFY(!contains(args, "--volume"));
        // the engine's view of volume under --silent is its own default —
        // the inert 42 is dropped by the mutual exclusion, not lost by
        // accident, so the read-back is compared against the engine view
        engine::Invocation engineView = invocation;
        engineView.volume = 15;
        compareInvocations(engineView, readBack(args));
    }

    void validateAcceptsCanonicalInvocations() {
        QVERIFY(engine::Validate(fullInvocation()).empty());
        // the fully-empty invocation carries no background at all — exactly
        // one diagnostic, the engine's own minimum-background rule
        const std::vector<engine::Diagnostic> empty = engine::Validate(engine::Invocation());
        QCOMPARE(empty.size(), size_t(1));
        QVERIFY(empty.front().flag.empty());
    }

    void validateMirrorsEngineConstraints() {
        auto flagOf = [](const std::vector<engine::Diagnostic>& problems) {
            std::string flags;
            for (const engine::Diagnostic& problem : problems) {
                if (!flags.empty())
                    flags += ",";
                flags += problem.flag;
            }
            return QString::fromStdString(flags);
        };

        engine::Invocation bad = fullInvocation();
        bad.screens[0].scaling = "cover"; // not an engine choice
        bad.screens[1].clamp = "mirror";  // not an engine choice
        bad.volume = 129;                 // engine clamps 0..128
        bad.fps = 0;                      // frozen frame
        bad.properties.push_back({"scheme=color", "1"}); // '=' would retarget the property
        const std::vector<engine::Diagnostic> problems = engine::Validate(bad);
        QVERIFY(problems.size() >= 5);
        QVERIFY(flagOf(problems).contains("--scaling"));
        QVERIFY(flagOf(problems).contains("--clamp"));
        QVERIFY(flagOf(problems).contains("--volume"));
        QVERIFY(flagOf(problems).contains("--fps"));
        QVERIFY(flagOf(problems).contains("--set-property"));

        engine::Invocation empty = fullInvocation();
        empty.screens.clear();
        empty.backgroundId.clear();
        const std::vector<engine::Diagnostic> noBackground = engine::Validate(empty);
        QCOMPARE(noBackground.size(), size_t(1));
        QVERIFY(noBackground.front().flag.empty());
    }

    // The drift sentinel: reconcile the contract against the engine's own
    // option registry whenever the hand-carried source is present. A new
    // upstream flag lands as an engine literal no table row covers; a
    // renamed one as a table row no literal covers. --help is argparse's
    // default argument (default_arguments::help), --property is the
    // registered alias of --set-property; both live outside the table.
    void contractMatchesEngineSource() {
#ifndef ENGINE_SOURCE_PATH
        QSKIP("engine source not present (clean checkout or CI fetch)");
#else
        std::ifstream file(ENGINE_SOURCE_PATH);
        QVERIFY(file.is_open());
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();

        static const std::regex longFlag(R"(--[A-Za-z][A-Za-z0-9-]*)");
        std::set<std::string> engineFlags;
        for (std::sregex_iterator it(source.begin(), source.end(), longFlag); it != std::sregex_iterator(); ++it)
            engineFlags.insert(it->str());

        static const std::set<std::string> outsideContract = {"--help", "--property"};
        std::set<std::string> expected(engineFlags);
        for (const std::string& known : outsideContract)
            expected.erase(known);

        const std::set<std::string> table = contractLongFlags();

        QStringList unknown;
        for (const std::string& flag : expected)
            if (table.count(flag) == 0)
                unknown << QString::fromStdString(flag);
        QStringList removed;
        for (const std::string& flag : table)
            if (engineFlags.count(flag) == 0)
                removed << QString::fromStdString(flag);

        QVERIFY2(unknown.isEmpty(),
                 qPrintable("engine flags missing from the contract table: " + unknown.join(", ")));
        QVERIFY2(removed.isEmpty(),
                 qPrintable("contract flags no longer in the engine source: " + removed.join(", ")));
#endif
    }
};

QTEST_MAIN(EngineTest)
#include "engine_test.moc"
