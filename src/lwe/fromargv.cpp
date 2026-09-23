#include "lwe/grammar.h"
#include "lwe/row.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace lwe {

namespace {

// 读回时的诊断一律算"引擎的语法规则"：文本本身不合语法，与产品策略无关。
void Report(std::vector<Diagnostic>* problems, const std::string& flag, const std::string& problem) {
    if (problems != nullptr)
        problems->push_back({flag, Rule::EngineInvariant, problem});
}

// 这个名字属于 kUnused 吗。认得它但从不发射，值得说清楚，而不是当成拼错的选项。
const UnusedOption* FindUnused(const std::string& token) {
    for (const UnusedOption& row : kUnused)
        if (FlagMatches(row.flags, token))
            return &row;
    return nullptr;
}

// --set-property 的载荷：引擎在第一个 '=' 处切分；整串没有 '=' 时键就是它、
// 值取 "1"（上游 action 的写法）
void WritePropertyToken(const Field& field, Arguments& arguments, const std::string& token) {
    const size_t equals = token.find('=');
    if (equals == std::string::npos)
        row::WriteProperty(field, arguments, token, "1");
    else
        row::WriteProperty(field, arguments, token.substr(0, equals), token.substr(equals + 1));
}

} // namespace

Arguments FromArgv(const std::vector<std::string>& argv, std::vector<Diagnostic>* problems) {
    Arguments arguments;
    // 当前绑定组的下标；npos = 还没遇到 --screen-root。
    // 用下标不用指针：screens 会增长，指针会失效。
    constexpr size_t noGroup = static_cast<size_t>(-1);
    size_t current = noGroup;

    for (size_t i = 0; i < argv.size(); i++) {
        const std::string& token = argv[i];
        const OptionSpec* spec = Find(token);
        if (spec == nullptr) {
            if (token.size() > 1 && token[0] == '-') {
                const UnusedOption* unused = FindUnused(token);
                Report(problems, token,
                       unused != nullptr ? std::string("不发射这个选项：") + unused->reason : "未知选项");
                // 认得它就得跳过它的取值，否则取值会被当成位置参数
                if (unused != nullptr && unused->arity != Arity::Flag && i + 1 < argv.size())
                    i++;
                continue;
            }
            // 裸 token：引擎的 "background id"（位置参数）
            if (!arguments.backgroundId.empty()) {
                Report(problems, "", "第二个位置参数 '" + token + "'");
                continue;
            }
            arguments.backgroundId = token;
            continue;
        }

        std::string value;
        if (spec->arity != Arity::Flag) {
            if (i + 1 >= argv.size()) {
                Report(problems, CanonicalSpelling(*spec), "缺少取值");
                break;
            }
            value = argv[++i];
        }

        switch (spec->binding) {
        case Binding::ScreenSwitch:
            arguments.screens.push_back(ScreenBinding());
            current = arguments.screens.size() - 1;
            arguments.screens[current].screen = value;
            break;
        case Binding::ScreenMember:
            if (current == noGroup) {
                Report(problems, CanonicalSpelling(*spec), "出现在任何 --screen-root 之前，没有绑定目标");
                break;
            }
            row::WriteScreenText(spec->field, arguments.screens[current], value);
            break;
        case Binding::Global:
            if (spec->arity == Arity::Flag) {
                // 极性决定写入的值：Negative 行（--no-*）见到即 false
                row::WriteFlag(spec->field, arguments, spec->polarity != Polarity::Negative);
            } else if (spec->arity == Arity::Repeated) {
                WritePropertyToken(spec->field, arguments, value);
            } else if (int number = 0; row::ParseInt(value, &number)) {
                row::WriteInt(spec->field, arguments, number);
            } else if (row::IntValue(spec->field, arguments) != nullptr) {
                Report(problems, CanonicalSpelling(*spec), "'" + value + "' 不是整数");
            } else {
                row::WriteText(spec->field, arguments, value);
            }
            break;
        case Binding::Positional:
            arguments.backgroundId = value;
            break;
        }
    }

    return arguments;
}

} // namespace lwe