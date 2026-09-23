#include "lwe/grammar.h"
#include "lwe/row.h"

#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace lwe {

namespace {

// 互斥组：同一组里只有表序最靠前的活跃行发射。
// --silent 排在 --volume 之前，所以 silent 赢 —— 这条顺序就是引擎的语义，
// 换表序就等于换行为，测试会看出来。
bool Suppressed(const OptionSpec& row, size_t index, const Arguments& arguments) {
    if (row.group == nullptr)
        return false;
    for (size_t i = 0; i < index; i++) {
        const OptionSpec& earlier = kGlobals[i];
        if (earlier.group != nullptr && std::strcmp(earlier.group, row.group) == 0 &&
            row::Active(earlier, arguments))
            return true;
    }
    return false;
}

// 一行 → 0..n 个 token
void Push(std::vector<std::string>& argv, const OptionSpec& row, const Arguments& arguments) {
    if (row.arity == Arity::Flag) {
        argv.push_back(CanonicalSpelling(row));
        return;
    }
    if (row.arity == Arity::Repeated) {
        const row::PropertyList* list = row::ListValue(row.field, arguments);
        if (list == nullptr)
            return;
        for (const std::pair<std::string, std::string>& property : *list) {
            argv.push_back(CanonicalSpelling(row));
            argv.push_back(property.first + "=" + property.second);
        }
        return;
    }
    if (const int* number = row::IntValue(row.field, arguments)) {
        argv.push_back(CanonicalSpelling(row));
        argv.push_back(std::to_string(*number));
        return;
    }
    if (const std::string* text = row::TextValue(row.field, arguments)) {
        argv.push_back(CanonicalSpelling(row));
        argv.push_back(*text);
    }
}

} // namespace

std::vector<std::string> ToArgv(const Arguments& arguments) {
    std::vector<std::string> argv;

    // 1) 屏幕组：先开关后成员。这个次序是引擎的绑定语法 —— --screen-root 把
    //    后随 --bg/--scaling/--clamp 的目标切到它，成员不能先于开关出现。
    const std::string screenSwitch = CanonicalSpelling(kScreenSwitch);
    for (const ScreenBinding& screen : arguments.screens) {
        argv.push_back(screenSwitch);
        argv.push_back(screen.screen);
        for (const OptionSpec& row : kScreenMembers) {
            const std::string* value = row::ScreenTextValue(row.field, screen);
            if (value == nullptr || value->empty())
                continue;
            argv.push_back(CanonicalSpelling(row));
            argv.push_back(*value);
        }
    }

    // 2) 全局行：表序 = 规范发射顺序
    for (size_t index = 0; index < std::size(kGlobals); index++) {
        const OptionSpec& row = kGlobals[index];
        if (!row::Active(row, arguments) || Suppressed(row, index, arguments))
            continue;
        Push(argv, row, arguments);
    }

    // 3) 位置参数：最后。argparse 允许它穿插，固定最后是为了字节唯一。
    if (!arguments.backgroundId.empty())
        argv.push_back(arguments.backgroundId);

    return argv;
}

} // namespace lwe