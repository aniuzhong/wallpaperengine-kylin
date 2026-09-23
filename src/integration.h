#pragma once

#include "error.h"
#include "result.h"

#include <cstdint>
#include <string>

// Desktop-integration management, the public facade. Keeps the desktop
// shell's window transparent so the wallpaper renders through it; the
// per-shell knowledge (peony, the UKUI desktop shell — one shell, one
// implementation, so a plain function family rather than an interface)
// lives in integration/peony.cpp.
//
// Setup re-runs are safe and act as self-healing (e.g. after the session
// respawns the shell without injection).
//
// Terminology: the deployed library is an interposer ("the shim"), not a
// code patch — see src/shim/peony-alpha.cpp for the precise mechanism.
namespace integration {

struct Status {
    int64_t peonyPid = 0; // 0 when the desktop shell is not running
    bool shimLoaded = false;
    bool Configured() const { return peonyPid != 0 && shimLoaded; }
};

// Scan /proc for the desktop shell process and check whether the shim is
// mapped into it.
Status Detect();

// Locate libpeony-alpha.so on disk: probe the frontend binary's own
// directory (build tree, and layouts that ship the pair together), the
// library directory a bin/ + lib/ install() layout produces, then the
// standard system library paths. Returns an empty string when nothing
// matches. Uses ShimCandidates() for the candidate list.
std::string LocateShim();

// Full integration pass:
//   1. point accountsservice/gsettings at a marker wallpaper rendered at
//      setup time into the user data dir — a programmatic "blue screen"
//      at the primary output's resolution (the shim nullifies it at load
//      time; if the injection is ever lost, the image on screen carries
//      the recovery instructions)
//   2. stop the shell, wait for exit (TERM, then KILL), clear the single-
//      instance lock so our injected instance wins the race
//   3. relaunch the shell via a transient systemd unit with LD_PRELOAD and
//      the shim environment (Restart=on-failure keeps it injected across
//      crashes)
//   4. verify: process alive, shim mapped
// A D-Bus failure keeps its kind and error name, a missing shim or marker
// file reports FileError, a launch that did not take reports Unknown.
we::Result<void> Setup();

// The exact inverse, and the only supported way out of the injection:
//   1. work out which wallpaper the desktop had before setup — from the copy
//      Setup() records, or from the environment of the shell that is still
//      running when that copy predates the installation
//   2. stop the transient unit that supervises the injected shell and wait
//      for the process to really exit
//   3. point accountsservice and gsettings back at that wallpaper, and drop
//      the files Setup() wrote
//   4. relaunch the shell as an ordinary detached process, with nothing
//      injected
//   5. verify: alive, shim gone
// A shell that is already running without the shim is left alone; only the
// wallpaper pointer is put back.
we::Result<void> Teardown();

} // namespace integration
