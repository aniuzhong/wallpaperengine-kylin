#include "display.h"

#include "utils/xcbutils.h"

#include <algorithm>

// The X server's own view of its outputs, read through the xcbutils port of
// KWin's RandR wrappers (KWin::Xcb). The wrappers own the reply lifecycle —
// this module only interprets it: an output without a driven crtc or modes
// has no name (the wrappers' own policy), and the primary sorts first.

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

std::vector<std::string> Outputs()
{
    KWin::Xcb::CurrentResources resources(KWin::Xcb::rootWindow());
    std::vector<std::string> names;
    if (resources) {
        const xcb_randr_output_t *outputs = resources.outputs();
        const int count = xcb_randr_get_screen_resources_current_outputs_length(resources.data());
        for (int i = 0; i < count; i++) {
            std::string name = KWin::Xcb::OutputInfo(outputs[i], XCB_CURRENT_TIME).name();
            if (!name.empty()) {
                names.push_back(std::move(name));
            }
        }
    }
    // primary first: a caller that just takes front() gets the desktop's own
    // idea of the main screen
    const std::string primary = primaryOutputName();
    const auto it = std::find(names.begin(), names.end(), primary);
    if (it != names.end()) {
        std::rotate(names.begin(), it, it + 1);
    }
    return names;
}

} // namespace display
