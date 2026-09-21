#pragma once

#include "error.h"

#include <cstdint>
#include <string>

// Desktop-integration management, the public facade. Keeps the desktop
// shell's window transparent so the wallpaper renders through it, as
// described by the Backend interface in integration/backend.h; the per-shell
// knowledge (peony today) lives behind that interface in
// integration/peony.cpp.
//
// The UI surfaces this as a banner with a single "Set up integration"
// button; setup re-runs are safe and act as self-healing (e.g. after the
// session respawns the shell without injection).
//
// Terminology: the deployed library is an interposer ("the shim"), not a
// code patch — see src/shim/peony-alpha.cpp for the precise mechanism.
namespace Integration {

struct Status {
    int64_t peonyPid = 0; // 0 when the desktop shell is not running
    bool shimLoaded = false;
    bool configured() const { return peonyPid != 0 && shimLoaded; }
};

// Scan /proc for the desktop shell process and check whether the shim is
// mapped into it.
Status detect();

// Locate libpeony-alpha.so on disk: probe the frontend binary's own
// directory (build tree, and layouts that ship the pair together), the
// library directory a bin/ + lib/ install() layout produces, then the
// standard system library paths. Returns an empty string when nothing
// matches.
std::string locateShim();

// Full integration pass:
//   1. point accountsservice/gsettings at a marker wallpaper generated
//      with libpng in the user data dir (the shim nullifies it at load
//      time — the color is irrelevant)
//   2. stop the shell, wait for exit (TERM, then KILL), clear the single-
//      instance lock so our injected instance wins the race
//   3. relaunch the shell via a transient systemd unit with LD_PRELOAD and
//      the shim environment (Restart=on-failure keeps it injected across
//      crashes)
//   4. verify: process alive, shim mapped
// Returns false with the failing step described in |error|: a D-Bus failure
// keeps its kind and error name, a missing shim or marker file reports
// FileError, a launch that did not take reports Unknown.
bool setup(wallpaper_engine::Error* error = nullptr);

// The exact inverse, and the only supported way out of the injection:
//   1. work out which wallpaper the desktop had before setup — from the copy
//      setup() records, or from the environment of the shell that is still
//      running when that copy predates the installation
//   2. stop the transient unit that supervises the injected shell and wait
//      for the process to really exit
//   3. point accountsservice and gsettings back at that wallpaper, and drop
//      the files setup() wrote
//   4. relaunch the shell as an ordinary detached process, with nothing
//      injected
//   5. verify: alive, shim gone
// A shell that is already running without the shim is left alone; only the
// wallpaper pointer is put back.
bool teardown(wallpaper_engine::Error* error = nullptr);

} // namespace Integration
