#include "process.h"

#include "posix.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <vector>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace process {

int RunCaptured(const std::string& program, const std::vector<std::string>& args,
                std::chrono::milliseconds timeout, std::string* output, bool* timedOut) {
    if (timedOut != nullptr)
        *timedOut = false;

    int outPipe[2] = { -1, -1 };
    int errPipe[2] = { -1, -1 };
    if (pipe(outPipe) != 0)
        return -1;
    if (pipe(errPipe) != 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return -1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        for (const int fd : { outPipe[0], outPipe[1], errPipe[0], errPipe[1] })
            close(fd);
        return -1;
    }
    if (pid == 0) {
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        for (const int fd : { outPipe[0], outPipe[1], errPipe[0], errPipe[1] })
            close(fd);
        std::vector<char*> childArgs;
        childArgs.push_back(const_cast<char*>(program.c_str()));
        for (const std::string& arg : args)
            childArgs.push_back(const_cast<char*>(arg.c_str()));
        childArgs.push_back(nullptr);
        execvp(program.c_str(), childArgs.data());
        _exit(127); // exec failed
    }

    close(outPipe[1]);
    close(errPipe[1]);
    const long long timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    const long long deadline = [] {
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
    }() + timeoutMs;

    int readers[2] = { outPipe[0], errPipe[0] };
    int openReaders = 2;
    char buffer[4096];
    while (openReaders > 0) {
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        const long long remaining =
            deadline - (static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000);
        if (remaining <= 0) {
            if (timedOut != nullptr)
                *timedOut = true;
            break;
        }
        pollfd fds[2] = { { readers[0], POLLIN, 0 }, { readers[1], POLLIN, 0 } };
        const int ready = poll(fds, 2, static_cast<int>(remaining));
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        for (int i = 0; i < 2; i++) {
            if ((fds[i].revents & (POLLIN | POLLHUP)) == 0)
                continue;
            const ssize_t n = read(readers[i], buffer, sizeof(buffer));
            if (n <= 0) {
                close(readers[i]);
                readers[i] = -1;
                openReaders--;
                continue;
            }
            if (output != nullptr)
                output->append(buffer, static_cast<size_t>(n));
        }
    }
    for (const int fd : readers)
        if (fd != -1)
            close(fd);

    const bool expired = timedOut != nullptr && *timedOut;
    if (expired)
        kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    if (expired)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool DidNotRun(int exitCode) {
    // -1: spawn failed or the deadline killed it. 127: execvp could not run
    // the path at all.
    return exitCode == -1 || exitCode == 127;
}

int RunAndWait(const std::vector<std::string>& argv) {
    if (argv.empty())
        return -1;
    const pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        std::vector<char*> childArgs;
        for (const std::string& arg : argv)
            childArgs.push_back(const_cast<char*>(arg.c_str()));
        childArgs.push_back(nullptr);
        execvp(argv.front().c_str(), childArgs.data());
        _exit(127); // exec failed
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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

    const pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        setsid();
        const pid_t grandchild = fork();
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
