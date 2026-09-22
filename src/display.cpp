#include "display.h"

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include <algorithm>

namespace {

// The output RandR calls primary, when it is actually driven. Empty when
// the server has no opinion or the primary is disconnected.
std::string PrimaryOutputName(xcb_connection_t* connection, const xcb_screen_t* screen) {
    if (screen == nullptr)
        return {};
    std::string name;
    xcb_randr_get_output_primary_reply_t* primary = xcb_randr_get_output_primary_reply(
        connection, xcb_randr_get_output_primary(connection, screen->root), nullptr);
    if (primary != nullptr) {
        xcb_randr_get_output_info_reply_t* info = xcb_randr_get_output_info_reply(
            connection, xcb_randr_get_output_info(connection, primary->output, XCB_CURRENT_TIME), nullptr);
        // a connected, currently-driven output carries the authoritative name
        if (info != nullptr && info->crtc != XCB_NONE && info->name_len > 0)
            name.assign(reinterpret_cast<const char*> (xcb_randr_get_output_info_name(info)), info->name_len);
        free(info);
        free(primary);
    }
    return name;
}

} // namespace

namespace display {

std::optional<std::string> PrimaryOutput() {
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        return std::nullopt;
    }

    const std::string primary =
        PrimaryOutputName(connection, xcb_setup_roots_iterator(xcb_get_setup(connection)).data);
    xcb_disconnect(connection);
    if (primary.empty())
        return std::nullopt;
    return primary;
}

std::vector<std::string> Outputs() {
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        return {};
    }

    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    std::vector<std::string> names;
    xcb_randr_get_screen_resources_current_reply_t* resources = xcb_randr_get_screen_resources_current_reply(
        connection, xcb_randr_get_screen_resources_current(connection, screen->root), nullptr);
    if (resources != nullptr) {
        const xcb_randr_output_t* outputs = xcb_randr_get_screen_resources_current_outputs(resources);
        const int count = xcb_randr_get_screen_resources_current_outputs_length(resources);
        for (int i = 0; i < count; i++) {
            xcb_randr_get_output_info_reply_t* info = xcb_randr_get_output_info_reply(
                connection, xcb_randr_get_output_info(connection, outputs[i], XCB_CURRENT_TIME), nullptr);
            // an unconnected connector has no crtc, and nothing can render on it
            if (info != nullptr && info->crtc != XCB_NONE && info->name_len > 0)
                names.push_back(std::string(
                    reinterpret_cast<const char*> (xcb_randr_get_output_info_name(info)), info->name_len));
            free(info);
        }
        free(resources);
    }
    // primary first: a caller that just takes front() gets the desktop's own
    // idea of the main screen — computed while the connection is still open
    const std::string primary = PrimaryOutputName(connection, screen);
    xcb_disconnect(connection);

    const auto it = std::find(names.begin(), names.end(), primary);
    if (it != names.end())
        std::rotate(names.begin(), it, it + 1);
    return names;
}

} // namespace display
