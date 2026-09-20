#pragma once

#include <cstdint>
#include <string>

// Desktop-integration management for the UKUI side of the stack: keeps
// peony-qt-desktop running with the interposition shim so the wallpaper
// renders through its transparent desktop window.
//
// The UI surfaces this as a banner with a single "Set up integration"
// button; setup re-runs are safe and act as self-healing (e.g. after the
// session respawns peony without injection).
namespace Integration {

struct Status {
    int64_t peonyPid = 0; // 0 when peony-qt-desktop is not running
    bool shimLoaded = false;
    bool configured () const { return peonyPid != 0 && shimLoaded; }
};

// Scan /proc for the peony desktop process and check whether the shim is
// mapped into it.
Status detect ();

// Locate libpeony-alpha-shim.so on disk: probe the frontend binary's own
// directory (build tree, and layouts that ship the pair together), the
// library directory a bin/ + lib/ install() layout produces, then the
// standard system library paths. Returns an empty string when nothing
// matches.
std::string locateShim ();

// Full integration pass (absorbs the former inject-peony.sh):
//   1. point accountsservice/gsettings at a marker wallpaper generated
//      with libpng in the user data dir (the shim nullifies it at load
//      time — the color is irrelevant)
//   2. stop peony, wait for exit (TERM, then KILL), clear the single-
//      instance lock so our injected instance wins the race
//   3. relaunch peony via a transient systemd unit with LD_PRELOAD and the
//      shim environment (Restart=on-failure keeps it injected across
//      crashes)
//   4. verify: process alive, shim mapped
// Returns false with details in |error| on failure.
bool setup(std::string* error);

} // namespace Integration
