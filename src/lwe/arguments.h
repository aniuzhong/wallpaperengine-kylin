#pragma once

#include <string>
#include <utility>
#include <vector>

// 参数集合：linux-wallpaperengine 一次调用的"参数那一半"。
//
// 刻意的缺席清单 —— 它们不在这个结构里，因为不属于命令行语法：
//   - argv[0]：程序路径归调用点（execvp 要它单独给，systemd 的 ExecStart 也
//     把它和参数拼在一起），模块只产出引擎看得见的那些参数；
//   - 环境与 cwd：DISPLAY / XAUTHORITY / HOME 由调用点决定，引擎据此解析
//     自己的资产与 Steam 布局；
//   - 引擎在 action 里派生出的状态：--bg 顺手改写默认背景、--screen-root 写
//     lastScreen —— 那是引擎解析器的内部状态机，不是语法。
//
// 它同时是 ToArgv 的输入与 FromArgv 的产物：双向共用同一份取值形状，
// 于是"对偶"可以用 ToArgv/FromArgv 的往返恒等式来判据。
namespace lwe {

// 一个 --screen-root 组：屏幕本身 + 绑定到它的成员。
// 空 scaling/clamp 表示"不发射" —— 引擎随后对这块屏用它自己那套默认。
struct ScreenBinding {
    std::string screen;
    std::string background; // --bg
    std::string scaling;    // --scaling
    std::string clamp;      // --clamp
};

struct Arguments {
    // 位置参数：引擎的 "background id" —— 没有 --bg 的屏幕回落到它，
    // 也是 --list-properties 的作用目标。
    std::string backgroundId;

    std::vector<ScreenBinding> screens; // 顺序 = 绑定顺序

    std::string assetsDir; // --assets-dir；空 = 不发射，引擎自己探测 Steam 布局

    int  fps = 30;                 // --fps，镜像引擎 argparse 的 default_value
    bool pauseOnFullscreen = true; // --no-fullscreen-pause：字段为 false 才发射
    bool automute = true;          // --noautomute：同上
    bool audioProcessing = true;   // --no-audio-processing：同上
    int  volume = 15;              // --volume，与 --silent 互斥（silent 优先）
    bool silent = false;           // --silent。引擎默认是出声，这里的默认是产品策略

    bool disableParticles = false;
    bool disableMouse = false;
    bool disableParallax = false;

    bool listProperties = false; // --list-properties，直接调用专用

    // --set-property 的可重复载荷。键不能含 '='：引擎在第一个 '=' 处切分，
    // 带 '=' 的键会静默作用到另一个属性上。
    std::vector<std::pair<std::string, std::string>> properties;
};

} // namespace lwe