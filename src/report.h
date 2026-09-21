#pragma once

#include "error.h"
#include "library.h"

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// The JSON projections of service data: one definition of every wire shape
// the CLI publishes with --json. Shaping JSON inside a command handler is
// how two commands end up publishing two contracts.
//
// Every function here is pure — values in, json out. Nothing reaches for
// the bus, the filesystem or the unit name; callers pass those in.
namespace report {

// {"status": {unit, state, screens: {screen: wallpaper}, enginePath}}
// |screens| is what the unit actually runs (engine_unit::UnitBackgrounds),
// which is the truth even after a manual unit edit.
nlohmann::json Status(const std::string& UnitName, const std::string& state,
                      const std::map<std::string, std::string>& screens, const std::string& enginePath);

// The entries wrapped in one array — the shape `list --json` has always
// published. Consumers index [0] for the entries; kept as-is so existing
// scripts keep working.
nlohmann::json Library(const std::vector<WallpaperEntry>& entries);

// {"error": {kind, message, dbusName?}} — the failure counterpart, so a
// --json consumer never has to parse prose. dbusName is present only when
// the failure came from the bus.
nlohmann::json Error(const wallpaper_engine::Error& error);

} // namespace report
