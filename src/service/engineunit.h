#pragma once

#include "config.h"

#include <QString>

// Thin control surface over the systemd user manager, backed by the typed
// D-Bus layer in src/systemd. The systemd user session is the runtime
// supervisor: the UI is only its editor, so closing the UI never affects a
// running wallpaper.
//
// The unit name can be overridden with WALLPAPER_ENGINE_UNIT(tests).
namespace EngineUnit {

QString unitName();                            // WALLPAPER_ENGINE_UNIT or "linux-wallpaperengine"
QString unitPath();                            // ~/.config/systemd/user/<unit>.service
QString unitFileContent(const Config& config); // unit text generated from config
QMap<QString, QString> unitBackgrounds();      // screen -> bg parsed from the unit's ExecStart
bool writeUnitFile(const Config& config);
bool daemonReload();
bool startUnit();
bool restartUnit();
bool stopUnit();
QString unitState();                           // active/inactive/failed/unknown

// The screen a single-screen(v1) desktop applies to: the primary X output
// as RandR reports it(the engine renders on X11), "DP-0" when headless.
QString fallbackScreenName();

// Single-screen v1: point the configured screen(or the fallback screen)
// at the wallpaper.
void assignScreen(Config& config, const QString& wallpaperId);

// The one apply chain shared by the UI and the CLI: persist the config,
// project it into the unit file, reload the manager, restart the unit.
bool applyConfig(const Config& config);

} // namespace EngineUnit
