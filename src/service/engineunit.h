#pragma once

#include "config.h"

#include <map>
#include <string>

// Thin control surface over the systemd user manager, backed by the typed
// D-Bus layer in src/service/systemd. The systemd user session is the
// runtime supervisor: the UI is only its editor, so closing the UI never
// affects a running wallpaper.
//
// The unit name can be overridden with WALLPAPER_ENGINE_UNIT (tests).
namespace EngineUnit {

std::string unitName ();                            // WALLPAPER_ENGINE_UNIT or "linux-wallpaperengine"
std::string unitPath ();                            // ~/.config/systemd/user/<unit>.service
std::string unitFileContent (const Config& config); // unit text generated from config
std::map<std::string, std::string> unitBackgrounds (); // screen -> bg parsed from the unit's ExecStart
bool writeUnitFile (const Config& config);
bool daemonReload ();
bool startUnit ();
bool restartUnit ();
bool stopUnit ();
std::string unitState ();                           // active/inactive/failed/unknown

// The screen a single-screen (v1) desktop applies to: the primary X output
// as RandR reports it (the engine renders on X11), "DP-0" when headless.
std::string fallbackScreenName ();

// Single-screen v1: point the configured screen (or the fallback screen)
// at the wallpaper.
void assignScreen (Config& config, const std::string& wallpaperId);

// The one apply chain shared by the UI and the CLI: persist the config,
// project it into the unit file, reload the manager, restart the unit.
bool applyConfig (const Config& config);

} // namespace EngineUnit
