#include "lwe/grammar.h"
#include "lwe/row.h"

#include <map>
#include <string>
#include <vector>

namespace lwe {

namespace {

// 产品策略管的那几行。写名字而不是下标：表会重排，名字不会；找不到行就说明
// 表改了名没改这里 —— lwe_test 里有一条断言盯着这三个名字还在。
constexpr const char* kFpsRow = "--fps";
constexpr const char* kVolumeRow = "--volume";
constexpr const char* kPropertyRow = "--set-property";

void Report(std::vector<Diagnostic>& problems, const std::string& flag, Rule source,
            const std::string& problem) {
    problems.push_back({flag, source, problem});
}

// 表的 choices 是 "a|b|c"
bool OneOf(const char* choices, const std::string& value) {
    const std::string list(choices);
    size_t start = 0;
    while (start <= list.size()) {
        const size_t bar = list.find('|', start);
        const size_t end = bar == std::string::npos ? list.size() : bar;
        if (list.compare(start, end - start, value) == 0)
            return true;
        if (bar == std::string::npos)
            break;
        start = bar + 1;
    }
    return false;
}

// 产品策略：引擎会接受（或静默修正）成别的东西，而我们要的是"要么照做，
// 要么报错"。这一半与引擎语法无关，上游变更时不必复核它。
void ValidatePolicies(const Arguments& arguments, std::vector<Diagnostic>& problems) {
    if (const OptionSpec* row = Find(kFpsRow); row != nullptr && arguments.fps < 1)
        Report(problems, CanonicalSpelling(*row), Rule::ProductPolicy,
               "必须 >= 1：引擎接受更小的值，但那是一帧不动的画面");

    if (const OptionSpec* row = Find(kVolumeRow);
        row != nullptr && (arguments.volume < 0 || arguments.volume > 128))
        Report(problems, CanonicalSpelling(*row), Rule::ProductPolicy,
               "必须在 0..128 内：引擎会静默截断越界值，那会让配置和实际听到的不符");
}

} // namespace

std::vector<Diagnostic> Validate(const Arguments& arguments, Scope scope) {
    std::vector<Diagnostic> problems;

    // 屏幕组：名、重复、背景、以及成员行自己的 choices
    std::map<std::string, int> screenCount;
    for (const ScreenBinding& screen : arguments.screens) {
        if (screen.screen.empty()) {
            Report(problems, CanonicalSpelling(kScreenSwitch), Rule::EngineInvariant, "屏幕名不能为空");
        } else if (++screenCount[screen.screen] > 1) {
            Report(problems, CanonicalSpelling(kScreenSwitch), Rule::EngineInvariant,
                   "屏幕 '" + screen.screen + "' 出现了多次，引擎会拒绝");
        }
        if (screen.background.empty())
            Report(problems, CanonicalSpelling(kScreenMembers[0]), Rule::EngineInvariant,
                   "屏幕 '" + screen.screen + "' 没有背景");

        for (const OptionSpec& row : kScreenMembers) {
            const std::string* value = row::ScreenTextValue(row.field, screen);
            if (value != nullptr && !value->empty() && row.choices != nullptr && !OneOf(row.choices, *value))
                Report(problems, CanonicalSpelling(row), Rule::EngineInvariant,
                       "'" + *value + "' 不在 " + row.choices + " 里");
        }
    }

    // 引擎起不来的条件：一个背景都没有（per-screen 的 --bg 或尾部的位置参数）
    bool anyBackground = !arguments.backgroundId.empty();
    for (const ScreenBinding& screen : arguments.screens)
        anyBackground = anyBackground || !screen.background.empty();
    if (!anyBackground)
        Report(problems, "", Rule::EngineInvariant, "至少要有一个背景：--bg 或尾部的位置参数");

    // 全局行：只在活跃时检查，规则全部从表读
    for (const OptionSpec& row : kGlobals) {
        if (!row::Active(row, arguments))
            continue;
        if (row.scope == Scope::DirectOnly && scope == Scope::Persistable)
            Report(problems, CanonicalSpelling(row), Rule::EngineInvariant,
                   "只用于按需诊断的直接调用，不进入 unit 文件");
        if (row.choices != nullptr) {
            const std::string* value = row::TextValue(row.field, arguments);
            if (value != nullptr && !OneOf(row.choices, *value))
                Report(problems, CanonicalSpelling(row), Rule::EngineInvariant,
                       "'" + *value + "' 不在 " + row.choices + " 里");
        }
    }

    // 可重复载荷的键：引擎在第一个 '=' 处切分，带 '=' 的键会作用到别的属性上
    if (const OptionSpec* row = Find(kPropertyRow)) {
        for (const std::pair<std::string, std::string>& property : arguments.properties) {
            if (property.first.empty())
                Report(problems, CanonicalSpelling(*row), Rule::EngineInvariant, "属性键不能为空");
            else if (property.first.find('=') != std::string::npos)
                Report(problems, CanonicalSpelling(*row), Rule::EngineInvariant,
                       "属性键 '" + property.first + "' 不能含 '='");
        }
    }

    ValidatePolicies(arguments, problems);
    return problems;
}

std::string Describe(const std::vector<Diagnostic>& problems) {
    std::string text;
    for (const Diagnostic& problem : problems) {
        if (!text.empty())
            text += "; ";
        text += problem.flag.empty() ? problem.problem : problem.flag + ": " + problem.problem;
    }
    return text;
}

} // namespace lwe