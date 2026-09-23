// Qt-free smoke test of the service archive plus the CLI frontend: links
// wallpaper_service and the CLI with NO Qt at all (no QtTest, no QtCore)
// and exercises the pure entry points. Guards the layer's Qt-free boundary
// — a single Qt include creeping into src/ (outside hook/) breaks this
// link.
#include "../src/projection.h"
#include "../src/lwe/grammar.h"
#include "../src/library.h"
#include "../src/exec_args.h"
#include "../src/cli.h"

#include <cstdio>
#include <string>
#include <vector>

int main() {
    // pure systemd-layer logic
    if (systemd::EscapeExecArg("a b") != "\"a b\"") {
        std::printf("smoke: EscapeExecArg failed\n");
        return 1;
    }
    const systemd::ExecCommand command =
        systemd::ToExecCommand(std::vector<std::string> { "/bin/tool", "x" });
    if (command.program != "/bin/tool" || command.args.size() != 2) {
        std::printf("smoke: ToExecCommand failed\n");
        return 1;
    }

    // config -> argument set -> argv, off the defaults (empty screens: no
    // screen groups). argv[0] belongs to the caller, so --fps leads here.
    const config::Config config;
    const std::vector<std::string> argv = lwe::ToArgv(projection::ToArguments(config));
    if (argv.empty() || argv.front() != "--fps") {
        std::printf("smoke: lwe::ToArgv failed\n");
        return 1;
    }

    // library scan on a nonexistent root degrades to an empty result
    if (!library::ScanLibrary("/wallpaper-engine-smoke-does-not-exist").empty()) {
        std::printf("smoke: library::ScanLibrary failed\n");
        return 1;
    }

    // CLI usage path: exercises the command table with no side effects
    if (cli::RunCli({}) != 2) {
        std::printf("smoke: cli::RunCli usage exit failed\n");
        return 1;
    }

    std::printf("service smoke: ok\n");
    return 0;
}
