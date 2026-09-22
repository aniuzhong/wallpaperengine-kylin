#pragma once

#include "exec_args.h" // ExecCommand
#include "result.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct sd_bus;

// The systemd user manager as a client library: one RAII D-Bus connection
// and a flat family of operations on it, plus a pure half (unit-id
// normalization, ActiveState parsing) with no bus access at all. Every
// fallible call returns Result<T> — the value or the one project-wide error
// (src/error.h) as a value; there are no out-params and nothing throws. The
// module knows nothing about wallpapers: callers feed unit ids and argv.
// The manager is addressed directly on the session bus, no systemctl
// subprocesses; one operation is one round trip, state is polled.
namespace systemd {

// Module-local spelling of the project-wide Result (src/error.h).
template <typename T>
using Result = we::Result<T>;

// ActiveState per systemd. The wire names parse at the boundary once
// (UnitStateFromName); callers compare enumerators, and anything new or
// unrecognized parses to Unknown rather than failing the call.
enum class UnitState {
    Active,
    Reloading,
    Inactive,
    Failed,
    Activating,
    Deactivating,
    Unknown,
};

UnitState UnitStateFromName(const std::string& name);
const char* UnitStateName(UnitState state); // the wire name, for display

// The D-Bus connection to the calling user's manager instance. Owns the
// sd_bus: move-only, flushed and closed on destruction. Every bus operation
// below takes it as its first parameter — the parameter list is the
// dependency declaration: no Connection, no bus access.
class Connection {
public:
    // Open the user bus. Fails (BusUnreachable) when no session bus is
    // reachable — headless runs, CI without enable-linger.
    static Result<Connection> UserBus();

    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // For the operation functions of this module only.
    sd_bus* handle() const noexcept { return bus_; }

private:
    Connection() = default;

    sd_bus* bus_ = nullptr;
};

// ---- unit lifecycle -------------------------------------------------------

Result<void> Start(Connection& connection, const std::string& unit);
Result<void> Stop(Connection& connection, const std::string& unit);
Result<void> Restart(Connection& connection, const std::string& unit);

// Advisory: a unit that is not loaded has nothing to reset — the manager
// errors on it, but callers mean "make sure it can start", so that outcome
// is success.
Result<void> ResetFailed(Connection& connection, const std::string& unit);

// Reload the manager so freshly written unit files are picked up.
Result<void> DaemonReload(Connection& connection);

// Value of an extra transient unit property: marshaled to its natural D-Bus
// type (string / boolean / 64-bit signed / double).
using UnitPropertyValue = std::variant<std::string, bool, int64_t, double>;

// Everything StartTransientUnit needs, named. The unit disappears with the
// session. The environment travels as /usr/bin/env argv prefixes rather
// than the transient Environment property: that property is not reliably
// applied to the exec'd process on all versions.
struct TransientSpec {
    std::string unit; // full unit id (CanonicalUnitName)
    std::vector<std::string> argv; // the command the unit runs
    std::vector<std::pair<std::string, std::string>> environment;
    std::map<std::string, UnitPropertyValue> extraProperties;
};

// Create and start a transient unit. A stale failed unit with the same name
// blocks re-creation ("already exists") and is reset first — a not-loaded
// unit is a harmless no-op.
Result<void> StartTransient(Connection& connection, const TransientSpec& spec);

// The unit's ActiveState. nullopt means the unit is not loaded (never
// started, stopped and unloaded, or simply nonexistent) — a distinct state,
// not folded into Inactive.
Result<std::optional<UnitState>> ActiveState(Connection& connection, const std::string& unit);

// Poll until the unit leaves active state (or unloads): the manager's stop
// is asynchronous, and a caller that stops a unit to replace it must wait
// for the departure to be real. Failed and unloaded count as left. Fails
// with Unknown past |timeout|.
Result<void> WaitInactive(Connection& connection, const std::string& unit,
                          std::chrono::milliseconds timeout);

// True when |error| is an acceptable outcome of an idempotent control
// operation: success, a unit that does not exist, or systemd's "not loaded"
// phrasing for the same thing.
bool Tolerated(const we::Error& error) noexcept;

} // namespace systemd
