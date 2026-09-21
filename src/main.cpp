// wallpaper-engine: the control surface of the Kylin integration around the
// linux-wallpaperengine engine (which keeps its upstream name on disk). The
// wallpaper itself runs under the user's systemd session
// and survives process exit; this binary only edits and supervises the unit,
// so the CLI needs no display at all and the browser frontend needs one only
// to show itself.
#include "cli.h"
#include "ui.h"

#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    // `ui` is dispatched here rather than in the CLI's command table: the CLI
    // is deliberately free of the HTTP server (the Qt-free smoke test links
    // cli.cpp on its own), and this keeps that boundary where it is.
    if (!args.empty() && args.front() == "ui")
        return Ui::runUi(std::vector<std::string> (args.begin() + 1, args.end()));

    return runCli(args);
}
