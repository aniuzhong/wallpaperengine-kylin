#pragma once

// I/O helpers shared by the service layer — the replacement for what
// QSaveFile's atomic-save and environment access used to provide. Nothing
// here depends on Qt. Location computation is inlined in the files that
// need it; this header only acts on paths it is handed.

#include "error.h"
#include "result.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace we {

inline std::string EnvOr(const char* name, const std::string& fallback) {
    const char* value = getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
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

} // namespace we
