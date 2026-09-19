#pragma once

#include "config.h"

#include <QString>

// Thin wrapper over `systemctl --user` managing the wallpaper-engine unit.
// The systemd user session is the runtime supervisor: the UI is only its
// editor, so closing the UI never affects a running wallpaper.
//
// The unit name can be overridden with WALLPAPER_ENGINE_UNIT (tests).
namespace Systemd {

QString unitName ();                            // WALLPAPER_ENGINE_UNIT or "wallpaper-engine"
QString unitPath ();                            // ~/.config/systemd/user/<unit>.service
QString unitFileContent (const Config& config); // unit text generated from config
bool writeUnitFile (const Config& config);
bool daemonReload ();
bool startUnit ();
bool restartUnit ();
bool stopUnit ();
bool resetFailedUnit ();
QString unitState ();                           // active/inactive/failed/unknown

} // namespace Systemd
