#include "engineprocess.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace EngineProcess {

int runCaptured(const std::string& enginePath, const std::vector<std::string>& args, long timeoutMs,
                std::string* output, bool* timedOut) {
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
        childArgs.push_back(const_cast<char*>(enginePath.c_str()));
        for (const std::string& arg : args)
            childArgs.push_back(const_cast<char*>(arg.c_str()));
        childArgs.push_back(nullptr);
        execvp(enginePath.c_str(), childArgs.data());
        _exit(127); // exec failed
    }

    close(outPipe[1]);
    close(errPipe[1]);
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

bool didNotRun(int exitCode) {
    // -1: spawn failed or the deadline killed it. 127: execvp could not run
    // the path at all.
    return exitCode == -1 || exitCode == 127;
}

} // namespace EngineProcess
