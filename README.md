# linux-wallpaperengine for Kylin V10 SP1

Run [Steam Workshop](https://steamcommunity.com/app/431960/workshop/)
wallpapers as the desktop background of Kylin V10 SP1 / UKUI (X11 session).

## Frontends

Both frontends sit on the same Qt-free service layer
(`wallpaper_service`), which owns the config file, the engine argv, the
systemd user unit and the peony integration. Neither frontend talks to
systemctl or writes unit files itself — they link the service and nothing
else, and the JSON they publish comes from the same pure projections in
`src/report.cpp`, so they cannot drift apart.

- **`wallpaper-engine <command>`** — the headless CLI. Works over SSH and
  with no display at all; `status --json` and `list --json` make it
  scriptable.
- **`wallpaper-engine ui`** — the browser frontend. A loopback-only HTTP
  server serves a single-page app (embedded in the binary) and opens it in
  the user's default browser with `--app=`, so it appears as its own window.
  It binds an OS-assigned port, requires a per-run session token, and exits
  once the page stops talking to it.

The wallpaper itself runs under the user's own systemd session, so closing
either frontend never affects a running wallpaper.

## Build

Qt is build-time only — the shim compiles against Qt headers (resolved from
the host peony process at load time) and the tests use QtTest. The shipped
binaries link no Qt at all.

Build dependencies: Qt5 Core/Gui, xcb-randr, libpng, libsystemd. Two more are
fetched over git at configure time: nlohmann/json and cpp-httplib.

    cmake -S . -B build
    cmake --build build
    cd build && ctest

The engine is a separate build and needs cmake >= 3.22 for its glslang
submodule (Kylin's 3.16 builds only the controller):

    cmake -S . -B build -DBUILD_ENGINE=ON

## Package

    docker buildx build --target export --output type=local,dest=out .

produces the .deb. The Kylin base image is provisioned by
`scripts/extract-base.sh` and pushed from a local machine; see the Dockerfile.

## Layout

    src/                service layer (Qt-free), CLI, browser frontend
    src/hook/           LD_PRELOAD shim for peony-qt-desktop
    src/ui/             the single-page app, embedded at configure time
    patches/            the engine patch series
    tests/              pure-logic tests, plus integration and smoke tests
