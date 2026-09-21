#pragma once

#include <string>

// The one error type every failing operation in the service layer reports
// through: the systemd/D-Bus layer, the config and unit files, the peony
// integration. Callers branch on |kind|; |message| and the preserved D-Bus
// name are for humans, logs and the --json error projection.
//
// Operations take `Error* error = nullptr` and return bool: callers that
// only need success/failure pass nothing, callers that report to a user
// pass a slot and get the reason.
namespace wallpaper_engine {

struct Error {
    enum Kind {
        NoError,
        BusUnreachable,  // no session bus, or no user manager on it
        NoSuchUnit,      // the manager does not know the unit
        JobFailed,       // the manager took the job and the job failed
        InvalidInput,    // caller error (empty argv, unusable argument)
        FileError,       // a file could not be read or written
        CorruptConfig,   // config.json exists but does not parse
        Unknown,
    };

    Kind kind = NoError;
    std::string dbusName; // D-Bus error name, when the failure came from one
    std::string message;  // human-readable detail

    bool ok() const { return kind == NoError; }
};

// Trailing ": <message>" for logging a non-ok error; empty for NoError.
inline std::string describe(const Error& error) {
    if (error.kind == Error::NoError)
        return {};
    return error.message.empty() ? std::string("unknown error") : error.message;
}

} // namespace wallpaper_engine
