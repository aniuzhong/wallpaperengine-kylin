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
| Integration backend (host process) | lowercase host name | `peony` |
| Interposer library + its env contract | `lib<host>-<effect>.so`, `<HOST>_<EFFECT>_WALLPAPER` | `libpeony-alpha.so`, `PEONY_ALPHA_WALLPAPER`; the shim log is always on, at `~/.local/share/wallpaper-engine/<host>/` (follows `XDG_DATA_HOME`) |
| Transient unit supervising an injected shell | `wallpaper-engine-<host>` | `wallpaper-engine-peony` |

The engine keeps its upstream name on disk: the `linux-wallpaperengine`
binary, and the deb payload directory `/opt/wallpaper-engine/` that holds
it next to `bin/wallpaper-engine`.

## Frontend

The CLI sits on the Qt-free service layer (`wallpaper_service`), which owns
the config file, the engine argv, the systemd user unit and the desktop
integration. The CLI never talks to systemctl or writes unit files itself —
it links the service and nothing else, and the JSON it publishes comes from
the pure projections in `src/report.cpp`.

- **`wallpaper-engine <command>`** — the headless CLI. Works over SSH and
  with no display at all; `status --json` and `list --json` make it
  scriptable.

The wallpaper itself runs under the user's own systemd session, so closing
the CLI never affects a running wallpaper.

## Build

Qt is build-time only — the shim compiles against Qt headers (resolved from
the host peony process at load time) and the tests use QtTest. The shipped
binaries link no Qt at all.

Build dependencies: Qt5 Core/Gui, xcb-randr, libpng, libsystemd. One more is
fetched over git at configure time: nlohmann/json.

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

    src/                    service layer (Qt-free) and the CLI
    src/integration/        the Backend seam and the peony backend
    src/shim/               libpeony-alpha.so, the LD_PRELOAD interposer
    icon/                   the project icon (the README header)
    patches/                the engine patch series
    tests/                  pure-logic tests, plus integration and smoke tests

## Style

Google C++ Style Guide with one deviation: 4-space indent. Docs say "the
shim" for the interposer; new identifiers follow the Names grammar above.
