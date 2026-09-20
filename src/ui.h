#pragma once

#include <string>
#include <vector>

// The browser frontend: a loopback-only HTTP server that republishes the
// service layer as JSON and serves the single-page app, plus the launch that
// opens it in the user's own browser.
//
// The server is deliberately short-lived. It binds 127.0.0.1 on an
// OS-assigned port, requires a per-run session token, and exits once the
// page stops talking to it — so the control surface exists while a window is
// open and not for a minute longer.
namespace Ui {

// `wallpaper-engine ui [--port N] [--no-open]`
int runUi(const std::vector<std::string>& args);

} // namespace Ui
