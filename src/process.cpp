#include "process.h"

#include "posix.h"

#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>

#include <sys/wait.h>
#include <unistd.h>

// Child processes run through reproc (fetched at configure time): start +
// drain + deadline instead of a hand-rolled fork/exec/poll loop — reproc's
// child side reports exec failures back over a pipe instead of the _exit(127)
// convention, and its drain enforces the deadline while polling. The one
// thing reproc does not provide is detach, so SpawnDetached keeps its own
// double fork + setsid.

namespace process {

int RunCaptured(const std::string& program, const std::vector<std::string>& args,
                std::chrono::milliseconds timeout, std::string* output, bool* timedOut) {
    if (timedOut != nullptr)
        *timedOut = false;

    std::vector<std::string> argv;
    argv.push_back(program);
    argv.insert(argv.end(), args.begin(), args.end());

    reproc::options options;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;
    options.deadline = std::chrono::duration_cast<reproc::milliseconds>(timeout);

    reproc::process child;
    if (std::error_code ec = child.start(argv, options))
        return -1; // spawn failed, or the exec failed and reproc reported it

    // Both streams append into one buffer in arrival order, like the polled
    // pipe loop this replaces; whatever was read before a timeout survives.
    auto append = [output](reproc::stream stream, const uint8_t* buffer,
                           size_t size) -> std::error_code {
        (void) stream;
        if (output != nullptr && size > 0)
            output->append(reinterpret_cast<const char*>(buffer), size);
        return {};
    };
    const std::error_code drained = reproc::drain(child, append, append);

    if (drained) {
        // The deadline (errc::timed_out) or a poll failure: drain has given
        // up, so nothing but an explicit kill stands between the child and
        // lingering forever.
        if (timedOut != nullptr && drained == std::errc::timed_out)
            *timedOut = true;
        (void) child.kill();
        (void) child.wait(reproc::milliseconds(1000));
        return -1;
    }

    // drain only returns cleanly once the child closed its pipes, so the
    // exit status is already queued
    int status = -1;
    std::error_code ec;
    std::tie(status, ec) = child.wait(reproc::infinite);
    return ec ? -1 : status;
}

bool DidNotRun(int exitCode) {
    // -1: spawn failed, the deadline killed the child, or reading its output
    // failed. (Exec failures surface as -1 too: reproc reports them through
    // start instead of the old _exit(127) convention.)
    return exitCode == -1 || exitCode == 127;
}

int RunAndWait(const std::vector<std::string>& argv) {
    if (argv.empty())
        return -1;

    reproc::options options;
    // the child keeps our stdout/stderr: a failing gsettings says why there
    options.redirect.parent = true;

    reproc::process child;
    if (std::error_code ec = child.start(argv, options))
        return -1;

    int status = -1;
    std::error_code ec;
    std::tie(status, ec) = child.wait(reproc::infinite);
    return ec ? -1 : status;
}

bool SpawnDetached(const std::vector<std::string>& argv) {
    // Whether |program| can be executed at all: an explicit path is checked
    // directly, a bare name is searched along PATH.
    const auto executableExists = [](const std::string& program) {
        if (program.empty())
            return false;
        if (program.find('/') != std::string::npos)
            return ::access(program.c_str(), X_OK) == 0;

        const std::string path = wallpaper_engine::EnvOr("PATH", "/usr/local/bin:/usr/bin:/bin");
        size_t start = 0;
        while (start <= path.size()) {
            const size_t end = path.find(':', start);
            const size_t stop = end == std::string::npos ? path.size() : end;
            if (stop > start && ::access((path.substr(start, stop - start) + "/" + program).c_str(), X_OK) == 0)
                return true;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        return false;
    };
    if (argv.empty() || !executableExists(argv.front()))
        return false;

    const pid_t pid = ::fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        setsid();
        const pid_t grandchild = ::fork();
        if (grandchild != 0)
            _exit(grandchild < 0 ? 1 : 0);

        std::vector<char*> args;
        for (const std::string& arg : argv)
            args.push_back(const_cast<char*>(arg.c_str()));
        args.push_back(nullptr);
        execvp(argv.front().c_str(), args.data());
        _exit(127); // exec failed
    }

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace process
