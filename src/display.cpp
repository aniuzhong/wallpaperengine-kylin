#include "display.h"

#include "utils/xcbutils.h"

// The X server's own view of its outputs, read through the xcbutils port of
// KWin's RandR wrappers (KWin::Xcb). The wrappers own the reply lifecycle —
// this module only interprets it: an output without a driven crtc or modes
// has no name (the wrappers' own policy).

namespace {

std::string primaryOutputName()
{
    KWin::Xcb::OutputPrimary primary(KWin::Xcb::rootWindow());
    if (!primary) {
        return {};
    }
    return KWin::Xcb::OutputInfo(primary->output, XCB_CURRENT_TIME).name();
}

} // namespace

namespace display {

std::optional<std::string> PrimaryOutput()
{
    const std::string primary = primaryOutputName();
    if (primary.empty()) {
        return std::nullopt;
    }
    return primary;
}

} // namespace display
