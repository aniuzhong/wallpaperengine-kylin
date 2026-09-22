#pragma once

// Minimal POSIX/environment helpers shared by the service layer — the
// replacements for what QStandardPaths, QThread and QCoreApplication used
// to provide. Nothing here depends on Qt.

#include "error.h"
#include "result.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

namespace wallpaper_engine {

inline std::string EnvOr(const char* name, const std::string& fallback) {
    const char* value = getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

inline std::string HomeDir() {
    const char* home = getenv("HOME");
    if (home != nullptr && *home != '\0')
        return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return pw->pw_dir;
    return {};
}

// Directory of the running executable — the applicationDirPath() replacement
// (empty when the link cannot be resolved).
inline std::string ExeDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    const std::string exe(buf);
    const size_t slash = exe.find_last_of('/');
    return slash == std::string::npos ? std::string() : exe.substr(0, slash);
}

// Replace |path| with |content| atomically: write a sibling temp file,
// fsync it, then rename over the target. A concurrent reader — or a crash —
// sees either the whole old file or the whole new one, never a truncated
// mix. The temp file is removed on every failure path.
inline Result<void> WriteFileAtomic(const std::string& path, const std::string& content) {
    const std::string temp = path + ".tmp";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        Error error;
        error.kind = Error::FileError;
        error.message = "cannot write " + temp + ": " + std::strerror(errno);
        return tl::unexpected(std::move(error));
    }

    size_t written = 0;
    while (written < content.size()) {
        const ssize_t n = ::write(fd, content.data() + written, content.size() - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            Error error;
            error.kind = Error::FileError;
            error.message = "cannot write " + temp + ": " + std::strerror(errno);
            ::close(fd);
            ::unlink(temp.c_str());
            return tl::unexpected(std::move(error));
        }
        written += static_cast<size_t>(n);
    }

    // fsync before the rename: without it a crash can leave the renamed
    // file present but empty on filesystems that reorder the data write
    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        Error error;
        error.kind = Error::FileError;
        error.message = "cannot flush " + temp + ": " + std::strerror(errno);
        ::unlink(temp.c_str());
        return tl::unexpected(std::move(error));
    }

    if (::rename(temp.c_str(), path.c_str()) != 0) {
        Error error;
        error.kind = Error::FileError;
        error.message = "cannot replace " + path + ": " + std::strerror(errno);
        ::unlink(temp.c_str());
        return tl::unexpected(std::move(error));
    }
    return {};
}

} // namespace wallpaper_engine
