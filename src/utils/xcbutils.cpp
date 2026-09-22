/*
    SPDX-FileCopyrightText: 2026 Aniuzhong

    SPDX-License-Identifier: GPL-2.0-or-later

    The Qt-free connection ownership behind the xcbutils port: upstream
    reaches the compositor's connection through the app singleton; here the
    service layer opens its own lazily and keeps it for the process
    lifetime. Short-lived CLI processes therefore leak one connection by
    design — the X server reaps it at process exit, and the wrappers never
    have to thread a connection through their APIs.
*/
#include "utils/xcbutils.h"

#include <xcb/xcb.h>

namespace KWin
{

namespace Xcb
{

xcb_connection_t *connection()
{
    static xcb_connection_t *c = xcb_connect(nullptr, nullptr);
    return c;
}

xcb_window_t rootWindow()
{
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection())).data;
    return screen ? screen->root : XCB_WINDOW_NONE;
}

} // namespace Xcb
} // namespace KWin
