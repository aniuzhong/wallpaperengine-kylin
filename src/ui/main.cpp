// wallpaper-engine: headless CLI control surface for the Kylin
// linux-wallpaperengine integration. The wallpaper itself runs under the
// user's systemd session and survives process exit; this binary only edits
// and supervises the unit, so it needs no display.
#include "cli.h"

#include <string>
#include <vector>

int main (int argc, char** argv) {
    return runCli (std::vector<std::string> (argv + 1, argv + argc));
}
