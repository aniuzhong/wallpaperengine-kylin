#pragma once

#include "config.h"
#include "error.h"
#include "result.h"

#include <map>
#include <string>

// The declared state: the unit file under the user manager's nose, what it
// says, and installing it. The systemd-format text knowledge lives here —
// the systemd module itself knows nothing about wallpapers or files, and
// this module knows nothing about buses.
namespace unit_file {

// Where the manager looks: ~/.config/systemd/user/<unit>.service.
// Deliberately $HOME-based, not XDG: the user manager must see this exact
// file, so XDG overrides from test shells must not relocate it. Tests
// isolate themselves by unit name instead.
std::string Path(const std::string& unit);

// Pure projection of the desired state into unit text. The XAUTHORITY path
// is injected by the caller — reading the session environment is the
// shell's job, and this function stays total and testable. sddm/gdm keep
// the cookie outside $HOME, which is why the path travels at all; an empty
// |xauthority| falls back to %h/.Xauthority.
std::string Text(const Config& config, const std::string& xauthority);

// The inverse projection: screen -> wallpaper as the installed unit file
// declares it (the truth even after manual unit edits). Empty when the
// file does not exist or declares nothing — that is a state, not an error.
std::map<std::string, std::string> Backgrounds(const std::string& unit);

// Atomic install: create the directory, then a sibling temp file + fsync +
// rename — the manager must never read a half-written unit.
wallpaper_engine::Result<void> Install(const Config& config, const std::string& unit);

} // namespace unit_file
