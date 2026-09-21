#pragma once

#include <string>

// The integration marker: a "blue screen" rendered at setup time — no
// image asset, no image library. The desktop's wallpaper pointer is aimed
// at it while the integration is active; the shim nullifies it at load
// time, so a working integration never shows it. When the injection is
// lost, the failure state on screen carries its own recovery instructions.
//
// Rendering is deliberately primitive: an RGB buffer, integer scaling and
// a bottom-up 24-bit BMP, which QImageReader loads without any codec —
// the service layer stays free of image libraries. Pure drawing: no bus,
// no display connection, fully unit-testable.
namespace Marker {

// Render at |width| x |height| and return the BMP file bytes. Integer
// scale is derived from the resolution (4K -> 6x, 1080p -> 3x, floor 1x).
std::string renderBmp(int width, int height);

// renderBmp + an atomic write to |path|.
bool writeTo(const std::string& path, int width, int height);

} // namespace Marker
