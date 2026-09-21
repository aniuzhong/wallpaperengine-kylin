<img src="icon/wallpaperengine-kylin.png" alt="" width="120" align="right">

# linux-wallpaperengine for Kylin V10 SP1

Run [Steam Workshop](https://steamcommunity.com/app/431960/workshop/)
wallpapers as the desktop background of Kylin V10 SP1 / UKUI (X11 session).

## Names

Every identifier in the tree follows one of the five grammars below, so new
names can be derived without discussion and old ones can be traced back to
an owner. Formal terms: the deployed library is an **interposer** ("the
shim") — it changes no code in memory, it works purely through ELF symbol
resolution (see `src/shim/peony-alpha.cpp`); "injection" refers only to
deploying it through the environment.

| Identifier class | Grammar | Examples |
|---|---|---|
| Product control plane | `wallpaper-engine` / `WALLPAPER_ENGINE_*` | binary, `~/.config/wallpaper-engine/`, `wallpaper-engine.service`, `WALLPAPER_ENGINE_UNIT` |
| App id (reverse-DNS, D-Bus-safe) | `io.github.aniuzhong.WallpaperEngine` | desktop file, bus name, icons, portal scopes |
| Integration backend (host process) | lowercase host name | `peony` |
| Interposer library + its env contract | `lib<host>-<effect>.so`, `<HOST>_<EFFECT>_<ROLE>` | `libpeony-alpha.so`, `PEONY_ALPHA_WALLPAPER`, `PEONY_ALPHA_LOG` |
| Transient unit supervising an injected shell | `wallpaper-engine-<host>` | `wallpaper-engine-peony` |

The engine keeps its upstream name on disk: the `linux-wallpaperengine`
binary, and the deb payload directory `/opt/wallpaper-engine/` that holds
it next to `bin/wallpaper-engine`.

## Frontends

Both frontends sit on the same Qt-free service layer
(`wallpaper_service`), which owns the config file, the engine argv, the
systemd user unit and the desktop integration. Neither frontend talks to
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

    src/                    service layer (Qt-free), CLI, browser frontend
    src/integration/        the Backend seam and the peony backend
    src/shim/               libpeony-alpha.so, the LD_PRELOAD interposer
    src/ui/                 the single-page app, embedded at configure time
    icon/                   the app icon: the 1024 master, and the 256 that ships
                            (launcher entry, pixmaps, hicolor, favicon)
    patches/                the engine patch series
    tests/                  pure-logic tests, plus integration and smoke tests

## Style

Google C++ Style Guide with one deviation: 4-space indent. Docs say "the
shim" for the interposer; new identifiers follow the Names grammar above.
