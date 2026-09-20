// Qt-free smoke test of the service archive: links wallpaper_service with
// NO Qt at all (no QtTest, no QtCore) and exercises the pure entry points.
// Guards the layer's Qt-free boundary — a single Qt include creeping into
// src/service breaks this link.
#include "../src/service/argvbuilder.h"
#include "../src/service/library.h"
#include "../src/service/systemd/unitbuilder.h"

#include <cstdio>
#include <string>
#include <vector>

int main () {
    // pure systemd-layer logic
    if (SystemdLayer::escapeExecArg ("a b") != "\"a b\"") {
        std::printf ("smoke: escapeExecArg failed\n");
        return 1;
    }
    const SystemdLayer::ExecCommand command =
        SystemdLayer::toExecCommand (std::vector<std::string> { "/bin/tool", "x" });
    if (command.program != "/bin/tool" || command.args.size () != 2) {
        std::printf ("smoke: toExecCommand failed\n");
        return 1;
    }

    // config -> argv mapping off the defaults (empty screens: no screen flags)
    const Config config;
    const std::vector<std::string> argv = buildArgv (config);
    if (argv.empty () || argv.front () != config.enginePath) {
        std::printf ("smoke: buildArgv failed\n");
        return 1;
    }

    // library scan on a nonexistent root degrades to an empty result
    if (!scanLibrary ("/lwe-smoke-does-not-exist").empty ()) {
        std::printf ("smoke: scanLibrary failed\n");
        return 1;
    }

    std::printf ("service smoke: ok\n");
    return 0;
}
