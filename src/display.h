#pragma once

#include <optional>
#include <string>
#include <vector>

// The X server's own view of its outputs, read from RandR. The engine
// renders on X11, so screen names must come from here — not from a GUI
// toolkit's QPA platform, whose names a Wayland-session GUI would not
// match to what the engine accepts. Policy (fallbacks, ordering guarantees
// beyond what is stated) lives with the callers; this module only reports.
namespace display {

// The output RandR calls primary, when it is actually driven: connected,
// with a crtc. nullopt when the server has no opinion or no X server
// answers at all.
std::optional<std::string> PrimaryOutput();

// Every output the server is currently driving (an output with a crtc —
// an unconnected connector has none, and nothing can render on it),
// primary first when the primary is among them. Empty when no X server
// answers.
std::vector<std::string> Outputs();

} // namespace display
