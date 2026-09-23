// T0 纯逻辑测试：lwe 语法表的两个解释器（ToArgv / FromArgv）与约束检查。
//
// expected 里的字节序列是重构前从当时的发射实现一次性冻结来的 golden：
// 它是"表驱动改写不改行为"的判据，所以逐 token 比对 —— 只看包含关系会让
// 绑定语法的回归（顺序）溜过去。
#include "../src/lwe/grammar.h"

#include <QtTest>

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string Join(const std::vector<std::string>& tokens) {
    std::string joined;
    for (const std::string& token : tokens) {
        if (!joined.empty())
            joined += " | ";
        joined += token;
    }
    return joined;
}

// 逐 token 比对：失败时能看出从第几个 token 起分歧，而不是"两串不一样"
void ExpectArgv(const std::vector<std::string>& actual, const std::vector<std::string>& expected) {
    if (actual.size() != expected.size()) {
        QCOMPARE(QString::fromStdString(Join(actual)), QString::fromStdString(Join(expected)));
        return;
    }
    for (size_t i = 0; i < expected.size(); i++)
        QCOMPARE(QString::fromStdString(actual[i]), QString::fromStdString(expected[i]));
}

// 字段指针必须指向真实成员：nullptr 会让取值当场崩，表里写错类型则取不到值
bool FieldIsBound(const lwe::Field& field) {
    return std::visit([](auto member) { return member != nullptr; }, field);
}

// 结构级还原：逐字段比对（含被互斥组压制的 volume，语料里刻意没踩那个例外）
void SameArguments(const lwe::Arguments& actual, const lwe::Arguments& expected) {
    QCOMPARE(QString::fromStdString(actual.backgroundId), QString::fromStdString(expected.backgroundId));
    QCOMPARE(QString::fromStdString(actual.assetsDir), QString::fromStdString(expected.assetsDir));
    QCOMPARE(actual.screens.size(), expected.screens.size());
    for (size_t i = 0; i < expected.screens.size() && i < actual.screens.size(); i++) {
        QCOMPARE(QString::fromStdString(actual.screens[i].screen), QString::fromStdString(expected.screens[i].screen));
        QCOMPARE(QString::fromStdString(actual.screens[i].background),
                 QString::fromStdString(expected.screens[i].background));
        QCOMPARE(QString::fromStdString(actual.screens[i].scaling), QString::fromStdString(expected.screens[i].scaling));
        QCOMPARE(QString::fromStdString(actual.screens[i].clamp), QString::fromStdString(expected.screens[i].clamp));
    }
    QCOMPARE(actual.fps, expected.fps);
    QCOMPARE(actual.pauseOnFullscreen, expected.pauseOnFullscreen);
    QCOMPARE(actual.automute, expected.automute);
    QCOMPARE(actual.audioProcessing, expected.audioProcessing);
    QCOMPARE(actual.volume, expected.volume);
    QCOMPARE(actual.silent, expected.silent);
    QCOMPARE(actual.disableParticles, expected.disableParticles);
    QCOMPARE(actual.disableMouse, expected.disableMouse);
    QCOMPARE(actual.disableParallax, expected.disableParallax);
    QCOMPARE(actual.listProperties, expected.listProperties);
    QCOMPARE(actual.properties.size(), expected.properties.size());
    for (size_t i = 0; i < expected.properties.size() && i < actual.properties.size(); i++) {
        QCOMPARE(QString::fromStdString(actual.properties[i].first), QString::fromStdString(expected.properties[i].first));
        QCOMPARE(QString::fromStdString(actual.properties[i].second),
                 QString::fromStdString(expected.properties[i].second));
    }
}

lwe::ScreenBinding Screen(const std::string& name, const std::string& background,
                          const std::string& scaling = std::string(), const std::string& clamp = std::string()) {
    lwe::ScreenBinding binding;
    binding.screen = name;
    binding.background = background;
    binding.scaling = scaling;
    binding.clamp = clamp;
    return binding;
}

// ---- 语料：每个用例都在覆盖某几行表 ----------------------------------------

lwe::Arguments DesktopDefault() {
    lwe::Arguments arguments;
    arguments.assetsDir = "/opt/wallpaper-engine/assets";
    arguments.screens = {Screen("DP-0", "123", "fill", "border"), Screen("DP-1", "456", "fill", "border")};
    arguments.silent = true;
    return arguments;
}

lwe::Arguments NegativeFlagsAndVolume() {
    lwe::Arguments arguments;
    arguments.screens = {Screen("HDMI-1", "7")};
    arguments.fps = 60;
    arguments.pauseOnFullscreen = false;
    arguments.automute = false;
    arguments.audioProcessing = false;
    arguments.volume = 42;
    return arguments;
}

lwe::Arguments DisableSwitchesAndProperties() {
    lwe::Arguments arguments;
    arguments.screens = {Screen("DP-0", "111")};
    arguments.silent = true;
    arguments.disableParticles = true;
    arguments.disableMouse = true;
    arguments.disableParallax = true;
    arguments.properties = {{"schemecolor", "0 0 0"}, {"speed", "2"}};
    return arguments;
}

lwe::Arguments PositionalAndEmptyOptionals() {
    lwe::Arguments arguments;
    arguments.screens = {Screen("DP-0", "111")};
    arguments.backgroundId = "999";
    arguments.silent = true;
    return arguments;
}

lwe::Arguments ListPropertiesDirect() {
    lwe::Arguments arguments;
    arguments.assetsDir = "/opt/we/assets";
    arguments.backgroundId = "77";
    arguments.listProperties = true;
    arguments.silent = true;
    return arguments;
}

lwe::Arguments NoScreens() {
    lwe::Arguments arguments;
    arguments.backgroundId = "123";
    arguments.silent = true;
    return arguments;
}

// 语料：每个用例覆盖某几行表，且都是合法调用（Validate 应当全绿）。
// "全默认值、没有背景"那个极端形状不进语料 —— 它本来就该被 Validate 拒绝，
// 由 engineDefaultsOnly 单独管它的发射形状。
std::vector<lwe::Arguments> Corpus() {
    return {DesktopDefault(),  NegativeFlagsAndVolume(), DisableSwitchesAndProperties(),
            PositionalAndEmptyOptionals(), ListPropertiesDirect(), NoScreens()};
}

// 表里所有长拼写（含 kUnused）—— 上游对账用
std::set<std::string> TableLongFlags() {
    std::set<std::string> flags;
    const auto collect = [&flags](const char* rowFlags) {
        const std::string text(rowFlags);
        size_t start = 0;
        while (start <= text.size()) {
            const size_t slash = text.find('/', start);
            const size_t end = slash == std::string::npos ? text.size() : slash;
            const std::string token = text.substr(start, end - start);
            if (token.rfind("--", 0) == 0)
                flags.insert(token);
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
    };
    collect(lwe::kScreenSwitch.flags);
    for (const lwe::OptionSpec& row : lwe::kScreenMembers)
        collect(row.flags);
    for (const lwe::OptionSpec& row : lwe::kGlobals)
        collect(row.flags);
    for (const lwe::UnusedOption& row : lwe::kUnused)
        collect(row.flags);
    return flags;
}

} // namespace

class LweTest : public QObject {
    Q_OBJECT

private slots:
    // ---- 正向：表驱动的发射 ------------------------------------------------

    void desktopDefault() {
        // 与切换前的字节相比，唯一的差异是 --assets-dir 落在屏幕组之后：
        // 全局行不读引擎的 lastScreen，位置对解析无影响；而"先屏幕组、后全局
        // 行"是 binding 分层定死的次序 —— 手写顺序正是这次改写要消掉的东西。
        ExpectArgv(lwe::ToArgv(DesktopDefault()),
                   {"--screen-root", "DP-0", "--bg", "123", "--scaling", "fill", "--clamp", "border",
                    "--screen-root", "DP-1", "--bg", "456", "--scaling", "fill", "--clamp", "border",
                    "--assets-dir", "/opt/wallpaper-engine/assets", "--fps", "30", "--silent"});
    }

    void negativeFlagsAndVolume() {
        // --no-* 三兄弟：字段为 false 才发射（Polarity::Negative）
        ExpectArgv(lwe::ToArgv(NegativeFlagsAndVolume()),
                   {"--screen-root", "HDMI-1", "--bg", "7", "--fps", "60", "--no-fullscreen-pause", "--noautomute",
                    "--no-audio-processing", "--volume", "42"});
    }

    void disableSwitchesAndProperties() {
        // 可重复行展开成 n 组 token；属性值里的空格原样保留（转义是通道层的事）
        ExpectArgv(lwe::ToArgv(DisableSwitchesAndProperties()),
                   {"--screen-root", "DP-0", "--bg", "111", "--fps", "30", "--silent", "--disable-particles",
                    "--disable-mouse", "--disable-parallax", "--set-property", "schemecolor=0 0 0",
                    "--set-property", "speed=2"});
    }

    void positionalAndEmptyOptionals() {
        // 空 assetsDir / 空 scaling / 空 clamp 一律不发射；位置参数固定最后
        ExpectArgv(lwe::ToArgv(PositionalAndEmptyOptionals()),
                   {"--screen-root", "DP-0", "--bg", "111", "--fps", "30", "--silent", "999"});
    }

    void listPropertiesDirect() {
        ExpectArgv(lwe::ToArgv(ListPropertiesDirect()),
                   {"--assets-dir", "/opt/we/assets", "--fps", "30", "--silent", "--list-properties", "77"});
    }

    void noScreens() {
        // 发射器不校验（空 screens 该被 Validate 拒绝），字节形状仍然确定
        ExpectArgv(lwe::ToArgv(NoScreens()), {"--fps", "30", "--silent", "123"});
    }

    void engineDefaultsOnly() {
        // 全显式：默认值也发射，unit 文件的 ExecStart 才自洽可逆
        ExpectArgv(lwe::ToArgv(lwe::Arguments()), {"--fps", "30", "--volume", "15"});
    }

    // ---- 反向：读回 --------------------------------------------------------

    void fromArgvRestoresTheArguments() {
        for (const lwe::Arguments& arguments : Corpus())
            SameArguments(lwe::FromArgv(lwe::ToArgv(arguments)), arguments);
    }

    void byteFormIsIdempotent() {
        for (const lwe::Arguments& arguments : Corpus()) {
            const std::vector<std::string> once = lwe::ToArgv(arguments);
            ExpectArgv(lwe::ToArgv(lwe::FromArgv(once)), once);
        }
    }

    // 结构级还原唯一的例外：互斥组压制的字段读不回来。
    // 这不是缺陷，是 silent 为真时 volume 根本不进 argv —— grammar.h 的
    // 恒等式注释里写明了这一条。
    void fromArgvDropsVolumeUnderSilent() {
        lwe::Arguments arguments = DesktopDefault();
        arguments.volume = 99;
        const lwe::Arguments back = lwe::FromArgv(lwe::ToArgv(arguments));
        QCOMPARE(back.silent, true);
        QCOMPARE(back.volume, 15); // 引擎默认值，也是 Arguments 的默认值
    }

    void fromArgvReportsUnknownAndUnused() {
        std::vector<lwe::Diagnostic> problems;
        const lwe::Arguments arguments =
            lwe::FromArgv({"--fps", "60", "--playlist", "mine.json", "77", "--nope"}, &problems);
        QCOMPARE(arguments.fps, 60);
        QCOMPARE(QString::fromStdString(arguments.backgroundId), QString("77"));
        // --playlist 认得但不发射（连同它的取值一起跳过）；--nope 未知
        QCOMPARE(problems.size(), size_t(2));
        QCOMPARE(QString::fromStdString(problems.front().flag), QString("--playlist"));
    }

    void fromArgvKeepsMemberBeforeSwitchVisiblyWrong() {
        std::vector<lwe::Diagnostic> problems;
        const lwe::Arguments arguments = lwe::FromArgv({"--bg", "123", "--fps", "30"}, &problems);
        QCOMPARE(problems.size(), size_t(1));
        QCOMPARE(QString::fromStdString(problems.front().flag), QString("--bg"));
        QVERIFY(arguments.screens.empty()); // 没有绑定目标，值无处可去
    }

    // ---- 约束 --------------------------------------------------------------

    void validateAcceptsTheCorpus() {
        for (const lwe::Arguments& arguments : Corpus()) {
            const lwe::Scope scope =
                arguments.listProperties ? lwe::Scope::DirectOnly : lwe::Scope::Persistable;
            const std::vector<lwe::Diagnostic> problems = lwe::Validate(arguments, scope);
            QVERIFY2(problems.empty(), lwe::Describe(problems).c_str());
        }
    }

    void validateMirrorsEngineConstraints() {
        lwe::Arguments bad = DesktopDefault();
        bad.screens[0].scaling = "stretchiness";         // 不在 choices 里
        bad.screens[0].clamp = "clampy";                 // 同上
        bad.volume = 999;                                // 引擎会静默截断
        bad.fps = 0;                                     // 冻帧
        bad.properties = {{"scheme=color", "1"}};        // '=' 会改写别的属性
        const std::vector<lwe::Diagnostic> problems = lwe::Validate(bad, lwe::Scope::Persistable);

        QStringList flags;
        for (const lwe::Diagnostic& problem : problems)
            flags << QString::fromStdString(problem.flag);
        QVERIFY(flags.contains("--scaling"));
        QVERIFY(flags.contains("--clamp"));
        QVERIFY(flags.contains("--volume"));
        QVERIFY(flags.contains("--fps"));
        QVERIFY(flags.contains("--set-property"));
        QVERIFY(problems.size() >= 5);
    }

    void validateRefusesDirectOnlyRowsInAUnitFile() {
        lwe::Arguments arguments = DesktopDefault();
        arguments.listProperties = true;
        const std::vector<lwe::Diagnostic> problems = lwe::Validate(arguments, lwe::Scope::Persistable);
        QCOMPARE(problems.size(), size_t(1));
        QCOMPARE(QString::fromStdString(problems.front().flag), QString("--list-properties"));
        // 同一个参数集合在直接调用作用域下合法：作用域就是那道闸
        QVERIFY(lwe::Validate(arguments, lwe::Scope::DirectOnly).empty());
    }

    void validateRequiresABackground() {
        const std::vector<lwe::Diagnostic> problems = lwe::Validate(lwe::Arguments(), lwe::Scope::Persistable);
        QCOMPARE(problems.size(), size_t(1));
        QVERIFY(problems.front().flag.empty());
        QCOMPARE(problems.front().source, lwe::Rule::EngineInvariant);
    }

    void validateMarksProductPolicySeparately() {
        lwe::Arguments arguments = DesktopDefault();
        arguments.fps = 0;
        const std::vector<lwe::Diagnostic> problems = lwe::Validate(arguments, lwe::Scope::Persistable);
        QCOMPARE(problems.size(), size_t(1));
        QCOMPARE(problems.front().source, lwe::Rule::ProductPolicy);
    }

    // ---- 表自身的账 --------------------------------------------------------

    void findMatchesBothNameForms() {
        QVERIFY(lwe::Find("--fps") != nullptr);
        QVERIFY(lwe::Find("-f") == lwe::Find("--fps")); // 别名命中同一行
        QCOMPARE(QString::fromStdString(lwe::CanonicalSpelling(*lwe::Find("--fps"))), QString("--fps"));
        QVERIFY(lwe::Find("--not-an-option") == nullptr);
        // kUnused 的名字不算命中：它们不在发射面上
        QVERIFY(lwe::Find("--playlist") == nullptr);
    }

    // 产品策略按名字指向表里的行；表改了名而这里没改，这条会红
    void policyRulesPointAtRealRows() {
        QVERIFY(lwe::Find("--fps") != nullptr);
        QVERIFY(lwe::Find("--volume") != nullptr);
        QVERIFY(lwe::Find("--set-property") != nullptr);
    }

    // 每个以 '-' 开头的 token 都必须是表里某行的合法拼写：发射器一旦手写了
    // 一个不在表里的 flag，这里就红
    void emittedFlagsAreTableFlags() {
        for (const lwe::Arguments& arguments : Corpus()) {
            for (const std::string& token : lwe::ToArgv(arguments)) {
                if (token.size() > 1 && token[0] == '-')
                    QVERIFY2(lwe::Find(token) != nullptr, token.c_str());
            }
        }
    }

    // 每一行可发射的表都必须被某个用例真正发射过一次，否则表里会出现
    // "写了但从没生效"的死行 —— 这类行在改名/迁移时最容易被漏掉。
    void everyRowIsExercised() {
        std::set<std::string> emitted;
        std::set<std::string> positionalValues;
        for (const lwe::Arguments& arguments : Corpus()) {
            if (!arguments.backgroundId.empty())
                positionalValues.insert(arguments.backgroundId);
            const std::vector<std::string> argv = lwe::ToArgv(arguments);
            emitted.insert(argv.begin(), argv.end());
        }

        QVERIFY2(emitted.count(lwe::CanonicalSpelling(lwe::kScreenSwitch)) == 1, lwe::kScreenSwitch.flags);
        for (const lwe::OptionSpec& row : lwe::kScreenMembers)
            QVERIFY2(emitted.count(lwe::CanonicalSpelling(row)) == 1, row.flags);
        for (const lwe::OptionSpec& row : lwe::kGlobals)
            QVERIFY2(emitted.count(lwe::CanonicalSpelling(row)) == 1, row.flags);
        for (const std::string& value : positionalValues)
            QVERIFY2(emitted.count(value) == 1, value.c_str());
    }

    // 表自身的良构性：拼写唯一、字段都绑上了、分层没有放错行。
    void tableIsWellFormed() {
        std::set<std::string> spellings;
        const auto unique = [&spellings](const lwe::OptionSpec& row) {
            return spellings.insert(lwe::CanonicalSpelling(row)).second;
        };

        QCOMPARE(lwe::kScreenSwitch.binding, lwe::Binding::ScreenSwitch);
        QVERIFY(unique(lwe::kScreenSwitch));
        QVERIFY(FieldIsBound(lwe::kScreenSwitch.field));

        for (const lwe::OptionSpec& row : lwe::kScreenMembers) {
            QCOMPARE(row.binding, lwe::Binding::ScreenMember);
            QVERIFY2(unique(row), row.flags);
            QVERIFY2(FieldIsBound(row.field), row.flags);
        }
        for (const lwe::OptionSpec& row : lwe::kGlobals) {
            QCOMPARE(row.binding, lwe::Binding::Global);
            QVERIFY2(unique(row), row.flags);
            QVERIFY2(FieldIsBound(row.field), row.flags);
        }

        QCOMPARE(lwe::kPositional.binding, lwe::Binding::Positional);
        QVERIFY(FieldIsBound(lwe::kPositional.field));

        // 别名也要唯一：否则 Find 会命中两行
        std::set<std::string> allNames;
        const auto everyName = [&allNames](const char* flags) {
            const std::string text(flags);
            size_t start = 0;
            while (start <= text.size()) {
                const size_t slash = text.find('/', start);
                const std::string name =
                    text.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!allNames.insert(name).second)
                    return false;
                if (slash == std::string::npos)
                    break;
                start = slash + 1;
            }
            return true;
        };
        QVERIFY(everyName(lwe::kScreenSwitch.flags));
        for (const lwe::OptionSpec& row : lwe::kScreenMembers)
            QVERIFY2(everyName(row.flags), row.flags);
        for (const lwe::OptionSpec& row : lwe::kGlobals)
            QVERIFY2(everyName(row.flags), row.flags);
        for (const lwe::UnusedOption& row : lwe::kUnused)
            QVERIFY2(everyName(row.flags), row.flags);
    }

    // 上游对账：表必须与引擎自己的选项注册表一致 —— 上游新增一个 flag 会
    // 表现为"引擎有、表没有"，改名则表现为"表有、引擎没有"。--help 是
    // argparse 的默认参数，--property 是 --set-property 的已登记别名。
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

        const std::set<std::string> table = TableLongFlags();

        QStringList unknown;
        for (const std::string& flag : expected)
            if (table.count(flag) == 0)
                unknown << QString::fromStdString(flag);
        QStringList removed;
        for (const std::string& flag : table)
            if (engineFlags.count(flag) == 0)
                removed << QString::fromStdString(flag);

        QVERIFY2(unknown.isEmpty(),
                 qPrintable("engine flags missing from the table: " + unknown.join(", ")));
        QVERIFY2(removed.isEmpty(),
                 qPrintable("table flags no longer in the engine source: " + removed.join(", ")));
#endif
    }
};

QTEST_MAIN(LweTest)
#include "lwe_test.moc"