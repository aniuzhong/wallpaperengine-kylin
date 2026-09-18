#pragma once

#include <QString>

// Desktop-integration management for the UKUI side of the stack: keeps
// peony-qt-desktop running with the interposition shim so the wallpaper
// renders through its transparent desktop window.
//
// The UI surfaces this as a banner with a single "Set up integration"
// button; setup re-runs are safe and act as self-healing (e.g. after the
// session respawns peony without injection).
namespace Integration {

struct Status {
    qint64 peonyPid = 0;  // 0 when peony-qt-desktop is not running
    bool shimLoaded = false;
    bool configured () const { return peonyPid != 0 && shimLoaded; }
};

// Scan /proc for the peony desktop process and check whether the shim is
// mapped into it.
Status detect ();

// Full integration pass (absorbs the former inject-peony.sh):
//   1. point accountsservice/gsettings at a generated marker wallpaper
//      (the shim nullifies it at load time — the color is irrelevant)
//   2. stop peony, wait for exit (TERM, then KILL), clear the single-
//      instance lock so our injected instance wins the race
//   3. relaunch peony via systemd-run --user with LD_PRELOAD and the
//      shim environment (Restart=on-failure keeps it injected across
//      crashes)
//   4. verify: process alive, shim mapped
// Returns false with details in |error| on failure.
bool setup (QString* error);

} // namespace Integration
