#pragma once

#include "error.h"
#include "library.h"

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// The JSON projections of service data: one definition of every wire shape,
// shared by the CLI (--json) and any other frontend. Shaping JSON inside a
// frontend is how two frontends end up publishing two contracts.
//
// Every function here is pure — values in, json out. Nothing reaches for
// the bus, the filesystem or the unit name; callers pass those in.
namespace Report {

// {"status": {unit, state, screens: {screen: wallpaper}, enginePath}}
// |screens| is what the unit actually runs (EngineUnit::unitBackgrounds),
// which is the truth even after a manual unit edit.
nlohmann::json status(const std::string& unitName, const std::string& state,
                      const std::map<std::string, std::string>& screens, const std::string& enginePath);

// The entries wrapped in one array — the shape `list --json` has always
// published. Consumers index [0] for the entries; kept as-is so existing
// scripts keep working.
nlohmann::json library(const std::vector<WallpaperEntry>& entries);

// {"error": {kind, message, dbusName?}} — the failure counterpart, so a
// --json consumer never has to parse prose. dbusName is present only when
// the failure came from the bus.
nlohmann::json error(const lwe::Error& error);

} // namespace Report
