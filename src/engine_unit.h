#pragma once

#include "config.h"
#include "error.h"
#include "result.h"

#include <map>
#include <string>
#include <vector>

// Thin control surface over the wallpaper engine's systemd unit: the unit
// identity, the declared-state projection (installed through unit_file),
// the lifecycle, and the one apply chain. Backed by the sd-bus client in
// systemd_unit.cpp — the user session is the runtime supervisor, the CLI
// only edits it, so exiting the CLI never affects a running wallpaper.
//
// The unit name can be overridden with WALLPAPER_ENGINE_UNIT (tests).
namespace engine_unit {

std::string UnitName();  // WALLPAPER_ENGINE_UNIT or "wallpaper-engine"
std::string UnitPath();  // ~/.config/systemd/user/<unit>.service

// Screen -> wallpaper as the installed unit file declares it (the truth
// even after manual unit edits); empty when nothing is declared yet.
std::map<std::string, std::string> UnitBackgrounds();

// Project the desired state into the unit file and install it atomically.
we::Result<void> WriteUnitFile(const config::Config& config);

we::Result<void> DaemonReload();
we::Result<void> StartUnit();
we::Result<void> RestartUnit();

// Idempotent: stopping a unit that was never loaded already has the
// desired end state and reports success.
we::Result<void> StopUnit();

// The unit's ActiveState as the wire spells it ("active"/"inactive"/...).
// A unit that is not loaded presents as "inactive" — the status contract
// predates the distinction; systemd::ActiveState is the honest view for
// callers that want it. Fails with Unknown when the bus does not answer.
we::Result<std::string> State();

// The primary X output as RandR reports it (the engine renders on X11);
// "DP-0" when no usable X server answers. Impure — it opens a display
// connection, so call it at the boundary and pass the name down.
std::string FallbackScreenName();

// Every output the X server is currently driving, primary first — the
// names --screen-root accepts. "DP-0" when no X server answers. Impure,
// like FallbackScreenName.
std::vector<std::string> ScreenNames();

// The one apply chain: persist the desired state, project it into the unit
// file the manager runs, reload, restart. The first failing step is the
// error the caller sees, and one bus connection spans reload + restart.
we::Result<void> ApplyConfig(const config::Config& config);

} // namespace engine_unit
