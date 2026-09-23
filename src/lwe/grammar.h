#pragma once

#include "arguments.h"

#include <variant>

// linux-wallpaperengine 的命令行语法，声明成数据。
//
// 对偶面在上游：third_party/linux-wallpaperengine/src/WallpaperEngine/Application/
// ApplicationContext.cpp 的 loadSettingsFromArgv() —— program.add_argument(...)
// 是声明链，每个 action 的 lambda 往 settings{} 里写值。本文件是那条声明链的
// 取值侧镜像：一行 = 一个引擎选项，Field 指向 Arguments 里它落在哪个成员上。
// 于是拼写、顺序、取值三件事都不必逐选项写代码。
//
// 边界（守住这些，模块才可测、可复核）：
//   - 只认引擎的命令行语法。不认识 config、systemd、文件系统；
//   - 不产出 argv[0]（程序路径归调用点），不做 systemd 文本转义 —— ExecStart
//     是文本通道，execvp 要的正是本模块产出的原始字符串，两边的转义规则不同，
//     混在一起就会出现字面反斜杠；
//   - 不复现 argparse action 的副作用（--bg 改写默认背景之类）。所以准确的
//     说法是"语法级对偶 + 状态级弱逆"：不要拿往返恒等式去要求那些派生状态。
//
// 拼写规则：flags 的第一段是规范拼写（也就是 ToArgv 实际发射的那一段），
// 其余段是查找用的别名。上游声明里短选项在前（"-f", "--fps"），这里反过来是
// 因为 argv 的字节由本表决定；对账比的是集合，不是顺序。
//
// 对账锚点：表声称的是 LWE_REF 锚点处的引擎 CLI（见 CMakeLists.txt）。
// 上游源码在场时，测试逐项比对集合与 choices —— 有了差异要重新对账，而不是
// 让表慢慢漂移成一份过时文档。
namespace lwe {

// 选项吃几个 token。
enum class Arity {
    Flag,     // 裸开关
    Value,    // 恰好一个值
    Repeated, // 可重复，每次一个值（上游的 .append()）
};

// 该行在语法里的角色 —— 决定发射顺序，以及谁绑定到谁。
enum class Binding {
    Global,       // 无绑定语义
    ScreenSwitch, // 开启一个屏幕组：重复出现即开启新的一组
    ScreenMember, // 写入当前屏幕组（引擎的 lastScreen）
    Positional,   // 尾部位置参数
};

// Flag 行的发射极性（只对 Arity::Flag 有意义）。
enum class Polarity {
    Positive, // 值为 true 才发射
    Negative, // 值为 false 才发射：引擎的 --no-* 三兄弟
};

// 这是谁的选择 —— Validate 与对账据此区分"引擎的规则"与"我们的策略"。
enum class Scope {
    Persistable, // 可以进入 systemd unit 的声明态
    DirectOnly,  // 只在按需诊断的直接调用里出现
};

// 一行的载荷：指向 Arguments / ScreenBinding 的成员。类型即取值类型，
// 于是取值与格式化都不必逐选项写代码。
using Field = std::variant<
    std::string Arguments::*,
    std::string ScreenBinding::*,
    int Arguments::*,
    bool Arguments::*,
    std::vector<std::pair<std::string, std::string>> Arguments::*>;

// 行的字段顺序：flags, arity, binding, polarity, scope, choices, group, field, note
struct OptionSpec {
    const char* flags;   // "规范拼写/别名/别名"
    Arity       arity;
    Binding     binding;
    Polarity    polarity;
    Scope       scope;
    const char* choices; // "a|b|c"；nullptr = 自由文本（上游的 .choices()）
    const char* group;   // 互斥组 id；nullptr = 不参与互斥
    Field       field;
    const char* note;    // 处置说明：为什么这样发射
};

// 上游有、我们不发射的选项。不在发射面上，但必须参与上游对账 ——
// 否则"漏了"和"故意不用"就分不开。arity 仍然要写对：读回一份手工改过的
// unit 文件时，只有知道它吃几个 token，才不会把它的取值误当成位置参数。
struct UnusedOption {
    const char* flags;
    Arity       arity;
    const char* reason;
};

// ---- 表工具 ----------------------------------------------------------------
// 规范拼写：flags 的第一段，也是 argv 里实际出现的那个字节，诊断里也用它当行的名字。
inline std::string CanonicalSpelling(const OptionSpec& row) {
    const std::string flags(row.flags);
    const size_t slash = flags.find('/');
    return slash == std::string::npos ? flags : flags.substr(0, slash);
}

// token 是否命中该行的任一段别名
inline bool FlagMatches(const char* flags, const std::string& token) {
    const std::string text(flags);
    size_t start = 0;
    while (start <= text.size()) {
        const size_t slash = text.find('/', start);
        const size_t end = slash == std::string::npos ? text.size() : slash;
        if (text.compare(start, end - start, token) == 0)
            return true;
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return false;
}

// ---- 屏幕层 ---------------------------------------------------------------
// 组开关是单例：这就是绑定语法本身。
inline const OptionSpec kScreenSwitch = {
    "--screen-root/-r", Arity::Value, Binding::ScreenSwitch, Polarity::Positive, Scope::Persistable,
    nullptr, nullptr, Field(&ScreenBinding::screen),
    "把后随成员行的绑定目标切到自己",
};

// 组内成员，顺序 = 组内发射顺序。空值 = 不发射。
inline const OptionSpec kScreenMembers[] = {
    {"--bg/-b", Arity::Value, Binding::ScreenMember, Polarity::Positive, Scope::Persistable, nullptr, nullptr,
     Field(&ScreenBinding::background), "空 = 该屏没指定，引擎回落到位置参数"},
    {"--scaling", Arity::Value, Binding::ScreenMember, Polarity::Positive, Scope::Persistable,
     "stretch|fit|fill|default", nullptr, Field(&ScreenBinding::scaling),
     "空 = 不发射，引擎用自己那套 per-screen 默认"},
    {"--clamp", Arity::Value, Binding::ScreenMember, Polarity::Positive, Scope::Persistable,
     "clamp|border|repeat", nullptr, Field(&ScreenBinding::clamp), "空 = 不发射"},
};

// ---- 全局层 ---------------------------------------------------------------
// 数组顺序 = 规范发射顺序，也就是 unit 文件里 ExecStart 的顺序。
// 规范形式（canonical）不是排版偏好：同一 Arguments 只有一种字节表示，
// 文本 diff 才等于语义 diff。
inline const OptionSpec kGlobals[] = {
    {"--assets-dir", Arity::Value, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::assetsDir), "空 = 不发射，引擎自己探测 Steam 布局"},
    {"--fps/-f", Arity::Value, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::fps), "int 型没有空值概念：始终发射（全显式）"},
    {"--no-fullscreen-pause", Arity::Flag, Binding::Global, Polarity::Negative, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::pauseOnFullscreen), "引擎默认 true，故字段为 false 才发射"},
    {"--noautomute", Arity::Flag, Binding::Global, Polarity::Negative, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::automute), "同上"},
    {"--no-audio-processing", Arity::Flag, Binding::Global, Polarity::Negative, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::audioProcessing), "同上"},
    {"--silent/-s", Arity::Flag, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr, "audio-out",
     Field(&Arguments::silent), "互斥组 audio-out：表序在前的活跃行赢"},
    {"--volume/-v", Arity::Value, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr, "audio-out",
     Field(&Arguments::volume), "被 --silent 压制；引擎会静默截断越界值，故由 Validate 拦"},
    {"--disable-particles", Arity::Flag, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::disableParticles), ""},
    {"--disable-mouse", Arity::Flag, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::disableMouse), ""},
    {"--disable-parallax", Arity::Flag, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr, nullptr,
     Field(&Arguments::disableParallax), ""},
    {"--set-property/--property", Arity::Repeated, Binding::Global, Polarity::Positive, Scope::Persistable, nullptr,
     nullptr, Field(&Arguments::properties), "k=v，可重复；裸键表示布尔 true"},
    {"--list-properties/-l", Arity::Flag, Binding::Global, Polarity::Positive, Scope::DirectOnly, nullptr, nullptr,
     Field(&Arguments::listProperties), "直接调用专用：持久化前由 Validate 拒绝"},
};

// 位置参数：最后发射（argparse 接受穿插，这是我们的规范形状）。
inline const OptionSpec kPositional = {
    "background id", Arity::Value, Binding::Positional, Polarity::Positive, Scope::Persistable, nullptr, nullptr,
    Field(&Arguments::backgroundId), "没有 --bg 的屏幕回落到它",
};

// 按任一段别名查找可发射的行。kUnused 里的名字不算命中：它们不在发射面上，
// 由上游对账单独覆盖。找不到返回 nullptr。
inline const OptionSpec* Find(const std::string& token) {
    if (FlagMatches(kScreenSwitch.flags, token))
        return &kScreenSwitch;
    for (const OptionSpec& row : kScreenMembers)
        if (FlagMatches(row.flags, token))
            return &row;
    for (const OptionSpec& row : kGlobals)
        if (FlagMatches(row.flags, token))
            return &row;
    if (FlagMatches(kPositional.flags, token))
        return &kPositional;
    return nullptr;
}

// ---- 解释器 ----------------------------------------------------------------
// 两个方向共用同一张表：正向把参数集合拍平成 argv，反向把 argv 读回参数集合。
// 恒等式分两条，测试各盯一条：
//   - 字节级幂等：ToArgv(FromArgv(t)) == t（规范文本出发，绕一圈回来还是它）。
//     ToArgv 是单射 —— 同一 a 只有一种字节表示，文本 diff 才等于语义 diff。
//   - 结构级还原：FromArgv(ToArgv(a)) == a，但被互斥组压制的字段除外
//     （silent 为真时 volume 根本不发射，读回来只能是默认值）。

// 正向：不含 argv[0]（程序路径归调用点）。纯函数，无 I/O。
std::vector<std::string> ToArgv(const Arguments& arguments);

// 一条诊断的出处：引擎的语法规则，还是我们的产品策略。
// 分开是为了上游一变就知道该复核哪一半。
enum class Rule {
    EngineInvariant,
    ProductPolicy,
};

struct Diagnostic {
    std::string flag;    // 出问题的行（规范拼写）；空 = 调用级规则
    Rule        source;
    std::string problem; // 说人话，点名规则
};

// 反向：读回 argv。顺序输入天然需要状态（--screen-root 开启一个组，随后的
// 成员行写进它），但状态机只认绑定，不复现 argparse action 的派生状态。
// problems 可为空；未知选项、缺取值写进它，合法的部分照样读出来。
Arguments FromArgv(const std::vector<std::string>& argv, std::vector<Diagnostic>* problems = nullptr);

// 约束检查。|scope| 是这次调用的作用域：Persistable 下 DirectOnly 的行被拒绝，
// DirectOnly 下放行 —— 于是"诊断开关混进 unit 文件"从靠自觉变成不可能。
std::vector<Diagnostic> Validate(const Arguments& arguments, Scope scope);

// 诊断列表 → 一行可读文本：每条 "<flag>: <problem>"，分号连接；无诊断得空串。
// 放在这里是为了让 unit 文件与直接调用两条失败路径发出同一句话。
std::string Describe(const std::vector<Diagnostic>& problems);

// ---- 我们不发射的选项 ------------------------------------------------------
inline const UnusedOption kUnused[] = {
    {"-w/--window", Arity::Value, "桌面服务没有窗口模式"},
    {"--screen-span", Arity::Value, "多屏用重复的 --screen-root 表达"},
    {"--playlist", Arity::Value, "读引擎自己的 config.json，是并行的另一套机制"},
    {"--layer", Arity::Value, "wayland-only"},
    {"--fullscreen-pause-only-active", Arity::Flag, "wayland-only"},
    {"--fullscreen-pause-ignore-appid", Arity::Value, "wayland-only，可重复"},
    {"--screenshot", Arity::Value, "诊断用：尚无调用者接线"},
    {"--screenshot-delay", Arity::Value, "诊断用：尚无调用者接线"},
    {"-z/--dump-structure", Arity::Flag, "诊断用：尚无调用者接线"},
    {"--render-debug", Arity::Value, "诊断用：尚无调用者接线"},
};

} // namespace lwe