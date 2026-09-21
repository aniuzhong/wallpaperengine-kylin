// wallpaper-engine: the control surface of the Kylin integration around the
// linux-wallpaperengine engine (which keeps its upstream name on disk). The
// wallpaper itself runs under the user's systemd session
// and survives process exit; this binary only edits and supervises the unit,
// so it needs no display at all.
#include "cli.h"

#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return RunCli(args);
}
