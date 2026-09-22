#pragma once

#include <string>

// The one error type every failing operation in the service layer reports
// through: the systemd/D-Bus layer, the config and unit files, the peony
// integration. Callers branch on |kind|; |message| and the preserved D-Bus
// name are for humans, logs and the --json error projection.
//
// Fallible operations return Result<T> (src/result.h) — the value or an
// Error, never both, never a thrown exception and never an out-param. Error
// itself has no success state: an operation that can always produce its
// value returns it plainly, and a diagnostics channel it carries stays
// optional (the one exception is Config::Load, which always recovers to
// defaults and reports the reason through a slot).
namespace wallpaper_engine {

struct Error {
    enum Kind {
        NoError,         // zero value: no failure recorded
        BusUnreachable,  // no session bus, or no user manager on it
        NoSuchUnit,      // the manager does not know the unit
        InvalidInput,    // caller error (empty argv, unusable argument)
        FileError,       // a file could not be read or written
        CorruptConfig,   // config.json exists but does not parse
        Unknown,
    };

    Kind kind = NoError;
    std::string dbusName; // D-Bus error name, when the failure came from one
    std::string message;  // human-readable detail
};

// Trailing ": <message>" for logging a non-ok error; empty for NoError.
inline std::string Describe(const Error& error) {
    if (error.kind == Error::NoError)
        return {};
    return error.message.empty() ? std::string("unknown error") : error.message;
}

} // namespace wallpaper_engine
