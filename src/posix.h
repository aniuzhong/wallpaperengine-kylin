#pragma once

// Minimal POSIX/environment helpers shared by the service layer — the
// replacements for what QStandardPaths, QThread and QCoreApplication used
// to provide. Nothing here depends on Qt.

#include <string>

#include <unistd.h>
#include <pwd.h>

namespace lwe {

inline std::string envOr(const char* name, const std::string& fallback) {
    const char* value = getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

inline std::string homeDir() {
    const char* home = getenv("HOME");
    if (home != nullptr && *home != '\0')
        return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return pw->pw_dir;
    return {};
}

// Directory of the running executable — the applicationDirPath() replacement
// (empty when the link cannot be resolved).
inline std::string exeDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    const std::string exe(buf);
    const size_t slash = exe.find_last_of('/');
    return slash == std::string::npos ? std::string() : exe.substr(0, slash);
}

inline void sleepMs(long ms) {
    timespec ts { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, nullptr);
}

} // namespace lwe
