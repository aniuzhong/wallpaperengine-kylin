#pragma once

#include "config.h"
#include "error.h"

#include <map>
#include <string>
#include <vector>

// Thin control surface over the systemd user manager, backed by the sd-bus
// client in systemd_unit.cpp. The systemd user session is the runtime
// supervisor: the CLI is only its editor, so exiting the CLI never affects
// a running wallpaper.
//
// The unit name can be overridden with WALLPAPER_ENGINE_UNIT (tests).
namespace engine_unit {

std::string UnitName();                            // WALLPAPER_ENGINE_UNIT or "wallpaper-engine"
std::string UnitPath();                            // ~/.config/systemd/user/<unit>.service
std::string UnitFileContent(const Config& config); // unit text generated from config
std::map<std::string, std::string> UnitBackgrounds(); // screen -> bg parsed from the unit's ExecStart
bool WriteUnitFile(const Config& config, wallpaper_engine::Error* error = nullptr);
bool DaemonReload(wallpaper_engine::Error* error = nullptr);
bool StartUnit(wallpaper_engine::Error* error = nullptr);
bool RestartUnit(wallpaper_engine::Error* error = nullptr);
bool StopUnit(wallpaper_engine::Error* error = nullptr);
std::string UnitState(wallpaper_engine::Error* error = nullptr); // active/inactive/failed/unknown

// The primary X output as RandR reports it (the engine renders on X11);
// "DP-0" when no usable X server answers. Impure — it opens a display
// connection, so call it at the boundary and pass the name down.
std::string FallbackScreenName();

// Every output the X server is currently driving, primary first — the names
// --screen-root accepts. An unconnected connector is left out (no crtc, so
// the engine cannot render on it); "DP-0" when no X server answers. Impure,
// like FallbackScreenName.
std::vector<std::string> ScreenNames();

// Pure: the screen a bare `switch` targets, given the desktop's primary
// output (pass FallbackScreenName()). Preference order — the primary output
// when the config already drives it, then the only configured screen, then
// the primary output. The rule this replaces ("whichever entry the map
// happened to yield first") followed std::map's ordering, not the desktop.
std::string DefaultScreenFor(const Config& config, const std::string& primaryOutput);

// Pure: point |screen| at |wallpaperId| and return the updated config. An
// empty screen or wallpaper leaves the config untouched (a screens[""]
// entry would be unmatchable by the engine).
Config AssignScreen(Config config, const std::string& screen, const std::string& wallpaperId);

// The one apply chain: persist the config, project it into the unit file,
// reload the manager, restart the unit. |error| carries the first step that
// failed.
bool ApplyConfig(const Config& config, wallpaper_engine::Error* error = nullptr);

} // namespace engine_unit
