#pragma once

#include "config.h"

#include <QString>

// Thin wrapper over `systemctl --user` managing the lwe-engine transient
// unit. The systemd user session is the runtime supervisor: the UI is only
// its editor, so closing the UI never affects a running wallpaper.
namespace Systemd {

QString unitPath ();                            // ~/.config/systemd/user/lwe-engine.service
QString unitFileContent (const Config& config); // unit text generated from config
bool writeUnitFile (const Config& config);
bool daemonReload ();
bool restartUnit ();                            // start or restart lwe-engine
bool stopUnit ();
QString unitState ();                           // active/inactive/failed/unknown

} // namespace Systemd
