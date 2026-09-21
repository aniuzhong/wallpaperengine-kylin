#pragma once

#include "error.h"

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace systemd {

// Value of an extra transient unit property: marshaled to its natural D-Bus
// type (string / boolean / 64-bit signed / double).
using UnitPropertyValue = std::variant<std::string, bool, int64_t, double>;

// One systemd user unit and its lifecycle, backed by the
// org.freedesktop.systemd1 D-Bus API on the session bus. Knows nothing
// about wallpapers: callers feed unit text and argv. State is read by
// polling activeState(); every lifecycle call is synchronous.
class SystemdUnit {
public:
    explicit SystemdUnit(std::string unitName);
    ~SystemdUnit();

    std::string unitName() const;

    // Transient unit: StartTransientUnit without touching the filesystem.
    // Disappears with the session; ideal for relaunched system components.
    // extraProperties: optional additional unit properties (key -> value).
    bool startTransient(const std::vector<std::string>& execArgs,
                        const std::map<std::string, std::string>& environment,
                        const std::map<std::string, UnitPropertyValue>& extraProperties, wallpaper_engine::Error* error = nullptr);

    bool start(wallpaper_engine::Error* error = nullptr);
    bool stop(wallpaper_engine::Error* error = nullptr);
    bool restart(wallpaper_engine::Error* error = nullptr);
    bool resetFailed(wallpaper_engine::Error* error = nullptr);

    // ActiveState per systemd: active / inactive / failed / activating.
    // A unit that is merely installed (not loaded) reads as inactive.
    std::string activeState(wallpaper_engine::Error* error = nullptr) const;
    bool isActive() const;

private:
    std::string m_unitName;
};

// Reload the user manager so freshly written unit files are picked up.
bool daemonReload(wallpaper_engine::Error* error = nullptr);

// True when the error is an acceptable outcome of an idempotent control
// operation: success, a unit that does not exist, or systemd's "not loaded"
// phrasing for the same situation.
bool tolerated(const wallpaper_engine::Error& error);

} // namespace systemd
