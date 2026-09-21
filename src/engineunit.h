#pragma once

#include "config.h"
#include "error.h"

#include <map>
#include <string>
#include <vector>

// Thin control surface over the systemd user manager, backed by the sd-bus
// client in systemdunit.cpp. The systemd user session is the runtime
// supervisor: the CLI is only its editor, so exiting the CLI never affects
// a running wallpaper.
//
// The unit name can be overridden with WALLPAPER_ENGINE_UNIT (tests).
namespace EngineUnit {

std::string unitName();                            // WALLPAPER_ENGINE_UNIT or "wallpaper-engine"
std::string unitPath();                            // ~/.config/systemd/user/<unit>.service
std::string unitFileContent(const Config& config); // unit text generated from config
std::map<std::string, std::string> unitBackgrounds(); // screen -> bg parsed from the unit's ExecStart
bool writeUnitFile(const Config& config, wallpaper_engine::Error* error = nullptr);
bool daemonReload(wallpaper_engine::Error* error = nullptr);
bool startUnit(wallpaper_engine::Error* error = nullptr);
bool restartUnit(wallpaper_engine::Error* error = nullptr);
bool stopUnit(wallpaper_engine::Error* error = nullptr);
std::string unitState(wallpaper_engine::Error* error = nullptr); // active/inactive/failed/unknown

// The primary X output as RandR reports it (the engine renders on X11);
// "DP-0" when no usable X server answers. Impure — it opens a display
// connection, so call it at the boundary and pass the name down.
std::string fallbackScreenName();

// Every output the X server is currently driving, primary first — the names
// --screen-root accepts. An unconnected connector is left out (no crtc, so
// the engine cannot render on it); "DP-0" when no X server answers. Impure,
// like fallbackScreenName.
std::vector<std::string> screenNames();

// Pure: the screen a bare `switch` targets, given the desktop's primary
// output (pass fallbackScreenName()). Preference order — the primary output
// when the config already drives it, then the only configured screen, then
// the primary output. The rule this replaces ("whichever entry the map
// happened to yield first") followed std::map's ordering, not the desktop.
std::string defaultScreenFor(const Config& config, const std::string& primaryOutput);

// Pure: point |screen| at |wallpaperId| and return the updated config. An
// empty screen or wallpaper leaves the config untouched (a screens[""]
// entry would be unmatchable by the engine).
Config assignScreen(Config config, const std::string& screen, const std::string& wallpaperId);

// The one apply chain: persist the config, project it into the unit file,
// reload the manager, restart the unit. |error| carries the first step that
// failed.
bool applyConfig(const Config& config, wallpaper_engine::Error* error = nullptr);

} // namespace EngineUnit
