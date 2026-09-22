#pragma once

#include <string>

// The integration marker: a "blue screen" rendered at setup time. The
// desktop's wallpaper pointer is aimed at it while the integration is
// active; the shim nullifies it at load time, so a working integration
// never shows it. When the injection is lost, the failure state on screen
// carries its own recovery instructions.
//
// Text is drawn with FreeType from the distribution's default English font
// (a fixed candidate list — no fontconfig, the recovery surface must not
// grow a discovery stack) and encoded as PNG through stb_image_write. The
// render never fails hard: without a usable font the screen is the plain
// background, and the write stays atomic either way. Pure drawing: no bus,
// no display connection, fully unit-testable.
namespace marker {

// True when a candidate font was found, so the rendered screen carries
// text; false means the background-only fallback. Exposed so tests can
// tell the two apart.
bool FontFound();

// Render at |width| x |height| and return the PNG file bytes. Integer
// scale is derived from the resolution (4K -> 6x, 1080p -> 3x, floor 1x).
std::string RenderPng(int width, int height);

// RenderPng + an atomic write to |path|.
bool WriteTo(const std::string& path, int width, int height);

} // namespace marker
