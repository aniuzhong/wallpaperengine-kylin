#pragma once

#include "../integration.h"

// The seam between the integration orchestrator (stop the desktop shell,
// relaunch it with the shim, verify, and the way back) and the knowledge of
// one specific shell: which process to look for, which settings point it at
// a wallpaper, how its single-instance locking behaves. One implementation
// per desktop shell.
//
// Names follow the project's scheme (see README, "Names"): the backend is
// the host process (peony), the library it deploys is <host>-<effect>
// (libpeony-alpha.so), the transient unit supervising it is
// wallpaper-engine-<host>.
namespace Integration {

class Backend {
public:
    virtual ~Backend() = default;

    virtual Status detect() = 0;
    virtual bool setup(wallpaper_engine::Error* error) = 0;
    virtual bool teardown(wallpaper_engine::Error* error) = 0;
};

// The peony (UKUI desktop) backend. Stateless: every call re-scans /proc,
// so a singleton is safe.
Backend& peonyBackend();

} // namespace Integration
