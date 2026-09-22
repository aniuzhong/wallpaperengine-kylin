#include "engine_unit.h"

#include "display.h"
#include "posix.h"
#include "systemd_unit.h"
#include "unit_file.h"

#include <utility>

namespace {

// overridable for tests (WALLPAPER_ENGINE_UNIT=<name>)
std::string UnitNameFromEnv() {
    static const std::string name = wallpaper_engine::EnvOr("WALLPAPER_ENGINE_UNIT", "wallpaper-engine");
    return name;
}

// One bus per operation: opened here, handed to the operation below — the
// operation's parameter list is where the bus dependency stays visible.
template <typename Op>
auto WithBus(Op&& op) -> decltype(op(std::declval<systemd::Connection&>())) {
    auto bus = systemd::Connection::UserBus();
    if (!bus)
        return tl::unexpected(std::move(bus).error());
    return op(*bus);
}

} // namespace

namespace engine_unit {

std::string UnitName() { return UnitNameFromEnv(); }

std::string UnitPath() { return unit_file::Path(UnitNameFromEnv()); }

std::map<std::string, std::string> UnitBackgrounds() {
    return unit_file::Backgrounds(UnitNameFromEnv());
}

wallpaper_engine::Result<void> WriteUnitFile(const Config& config) {
    return unit_file::Install(config, UnitNameFromEnv());
}

// every lifecycle operation goes through the typed sd-bus layer — the
// manager is addressed directly on the session bus, no systemctl subprocesses
wallpaper_engine::Result<void> DaemonReload() {
    return WithBus([](systemd::Connection& bus) { return systemd::DaemonReload(bus); });
}

wallpaper_engine::Result<void> StartUnit() {
    return WithBus([](systemd::Connection& bus) { return systemd::Start(bus, UnitNameFromEnv()); });
}

wallpaper_engine::Result<void> RestartUnit() {
    return WithBus([](systemd::Connection& bus) { return systemd::Restart(bus, UnitNameFromEnv()); });
}

wallpaper_engine::Result<void> StopUnit() {
    return WithBus([](systemd::Connection& bus) -> wallpaper_engine::Result<void> {
        auto stopped = systemd::Stop(bus, UnitNameFromEnv());
        // stop/reset-failed on a unit that was never loaded already has the
        // desired end state: systemd reports NoSuchUnit ("not loaded",
        // systemctl's old exit code 5). Treat it as success — keeps the CLI
        // idempotent for scripting.
        if (!stopped && systemd::Tolerated(stopped.error()))
            return {};
        return stopped;
    });
}

wallpaper_engine::Result<std::string> State() {
    return WithBus([](systemd::Connection& bus) -> wallpaper_engine::Result<std::string> {
        auto state = systemd::ActiveState(bus, UnitNameFromEnv());
        if (!state)
            return tl::unexpected(std::move(state).error());
        // a not-loaded unit presents as inactive: the status contract
        // predates the distinction, and scripts compare the old token —
        // systemd::ActiveState is the honest view for callers that want it
        return systemd::UnitStateName(state->value_or(systemd::UnitState::Inactive));
    });
}

std::string FallbackScreenName() {
    // the engine renders on X11, so the name must come from the X server's
    // own RandR view (display::). "DP-0" stays the fallback for headless
    // runs (no usable X server at all) — the policy lives here, the
    // platform only reports.
    return display::PrimaryOutput().value_or("DP-0");
}

std::vector<std::string> ScreenNames() {
    auto names = display::Outputs();
    if (names.empty())
        names = { "DP-0" };
    return names;
}

wallpaper_engine::Result<void> ApplyConfig(const Config& config) {
    // the one apply chain: persist the desired state, project it into the
    // unit file the manager runs, reload, restart. The first failing step
    // is the error the caller sees, and one bus connection spans the
    // reload + restart pair.
    if (auto saved = config.Save(); !saved)
        return saved;
    if (auto installed = WriteUnitFile(config); !installed)
        return installed;
    return WithBus([](systemd::Connection& bus) -> wallpaper_engine::Result<void> {
        if (auto reloaded = systemd::DaemonReload(bus); !reloaded)
            return reloaded;
        return systemd::Restart(bus, UnitNameFromEnv());
    });
}

} // namespace engine_unit
