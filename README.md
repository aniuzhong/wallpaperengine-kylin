# linux-wallpaperengine for Kylin V10 SP1

Run [Steam Workshop](https://steamcommunity.com/app/431960/workshop/)
wallpapers as the desktop background of Kylin V10 SP1 / UKUI (X11 session),
with desktop icons, right-click and rubber-band selection fully working.

The deliverable is a single `.deb`; the GitHub pipeline builds it from the
pinned upstream commit plus the patch series in `patches/`.

## Install

    sudo dpkg -i linux-wallpaperengine-kylin_<version>_amd64.deb
    sudo apt-get install -f   # if runtime libs are missing

Installs into `/opt/linux-wallpaperengine/` (engine, `bin/wallpaper-engine`
control binary, `lib/peony-alpha-shim.so`).

## One-time session setup

    wallpaper-engine setup-integration

This restarts the UKUI desktop (peony) with the interposition shim that
makes its background transparent, so the wallpaper below shows through.
Your icons/menus keep working; the command is safe to re-run (self-healing).

## Daily use (headless — no UI needed)

    wallpaper-engine start | stop | restart
    wallpaper-engine status [--json]
    wallpaper-engine list [--json]
    wallpaper-engine switch <id|--random>     # persists across restarts
    wallpaper-engine doctor                   # diagnostics

The wallpaper runs as the `wallpaper-engine` systemd **user** unit with
`Restart=on-failure`: it survives UI exit, crashes and screen locks, and is
restarted automatically if the engine dies. `wallpaper-engine start` with no
unit installed installs one from `~/.config/lwe-dynamic-wallpaper/config.json`.

Start the GUI (thumbnail grid, double-click to apply) by running
`wallpaper-engine` with no arguments.

## Build from source

Requirements: Kylin V10 SP1 (or any Ubuntu 20.04-based userland) with
qtbase5-dev, libglew-dev, libmpv-dev, libglfw3-dev, and CMake >= 3.22.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build            # unit + integration tests

The engine itself is NOT built from this repository's tree: `Dockerfile`
fetches upstream at the pinned `LWPE_REF`, applies `patches/*.patch` in
lexicographic order and installs it into the deb. `scripts/extract-base.sh`
provisions the Kylin base image used by CI from the official install media.

## Architecture

```
control plane   wallpaper-engine (GUI/CLI)  — edits config, drives systemd
supervision     systemd --user (wallpaper-engine.service, Restart=on-failure)
render plane    linux-wallpaperengine (engine process, upstream + patches)
integration     peony-alpha-shim.so (LD_PRELOAD) — transparent desktop window
                ukui-kwin keeps the engine (DESKTOP layer) under peony (BELOW)
```

The shim intercepts two points in peony: the `QPixmap` constructor loading
the background (replaced with a transparent pixmap) and the
`_NET_WM_WINDOW_TYPE` property write (DESKTOP is rewritten to NORMAL
pre-map, so ukui-kwin composites it translucently without a WM restart).

## Troubleshooting

- `wallpaper-engine doctor` reports every component's state.
- Shim log: `~/.local/share/lwe-dynamic-wallpaper/peony-shim.log`.
- Unit logs: `journalctl --user -u wallpaper-engine`.
- If the desktop shows magenta, the shim is not loaded — re-run
  `setup-integration`.

## Uninstall

    sudo apt-get remove linux-wallpaperengine-kylin

Stops the units; your original wallpaper choice is preserved in
`~/.config/lwe-dynamic-wallpaper/original-background`.
