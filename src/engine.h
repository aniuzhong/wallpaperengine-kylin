#pragma once

#include <string>
#include <utility>
#include <vector>

// Pure model of linux-wallpaperengine's command line: the option contract
// (kOptions), the runtime invocation (Invocation), and the two actions over
// it — Validate checks an invocation against the contract, Emit flattens it
// into argv in the binding order the engine's argparse expects.
//
// Grammar anchor: third_party/linux-wallpaperengine/src/WallpaperEngine/
// Application/ApplicationContext.cpp, loadSettingsFromArgv(). The engine
// source is deliberately not pinned (the third_party checkout is carried by
// hand, CI fetches the LWPE_REF anchor), so this table claims to be "the
// engine CLI as of the last reconciliation", never "the engine CLI" —
// re-reconcile when bumping the engine, and tests/engine_test.cpp checks
// the drift mechanically whenever the engine source is present.
//
// Boundaries, stated so they can be defended: the module knows the engine's
// command-line grammar and nothing else. It does not know config::Config
// (the projection lives in argvbuilder), systemd (ExecStart escaping lives
// in exec_args), or the filesystem (path and Steam layout resolution is
// engine-internal behavior the engine performs itself). No I/O, no bus, no
// global state — data in, data out, every function pure.
namespace engine {

// ---- contract --------------------------------------------------------------
//
// Compile-time data. All-struct by decision: this module holds no invariant-
// protected state, so there is nothing to encapsulate — legality is detected
// by Validate, not made unrepresentable by constructors, and the contract
// table must stay constexpr-aggregate.

enum class Arity {
    Flag,  // boolean switch: emitted bare or not at all
    Value, // takes one argument
};

// One row per linux-wallpaperengine option. |choices| is the engine's
// argparse .choices() list, "|"-separated, or null for free text. |note|
// records the disposition so coverage of the engine surface stays explicit:
//   "config:<field>"      a config::Config field drives it
//   "direct:<caller>"     a command builds it on demand, never via config
//   "unused:<reason>"     the Kylin service deliberately never emits it
struct OptionSpec {
    const char* flag;    // engine name, slash-combined when aliased: "-f/--fps"
    Arity arity;
    const char* choices;
    const char* note;
};

inline constexpr OptionSpec kOptions[] = {
    // Background options
    {"background id",        Arity::Value, nullptr,                         "positional; unused: per-screen --bg drives it"},
    {"-w/--window",          Arity::Value, nullptr,                         "unused: no window mode in the desktop service"},
    {"-r/--screen-root",     Arity::Value, nullptr,                         "config:screens; switches the binding target for --bg/--scaling/--clamp"},
    {"--screen-span",        Arity::Value, nullptr,                         "unused: multi-screen via repeated --screen-root"},
    {"-b/--bg",              Arity::Value, nullptr,                         "config:screens; binds to the preceding --screen-root"},
    {"--playlist",           Arity::Value, nullptr,                         "unused: reads the engine's own config.json, a parallel mechanism"},
    {"--scaling",            Arity::Value, "stretch|fit|fill|default",      "config:scaling; binds to the preceding --screen-root"},
    {"--clamp",              Arity::Value, "clamp|border|repeat",           "config:clamp; binds to the preceding --screen-root"},
    {"--layer",              Arity::Value, "background|bottom|top|overlay", "unused: wayland-only"},
    // Performance options
    {"-f/--fps",             Arity::Value, nullptr,                         "config:fps"},
    {"--no-fullscreen-pause", Arity::Flag, nullptr,                         "config:fullscreenPause, emitted when false"},
    {"--fullscreen-pause-only-active", Arity::Flag, nullptr,                   "unused: wayland-only"},
    {"--fullscreen-pause-ignore-appid", Arity::Value, nullptr,                 "unused: wayland-only, repeatable"},
    // Sound options
    {"-v/--volume",          Arity::Value, nullptr,                         "config:volume, engine clamps 0-128 — we surface out-of-range instead"},
    {"-s/--silent",          Arity::Flag,  nullptr,                         "config:silent, mutually exclusive with --volume"},
    {"--noautomute",         Arity::Flag,  nullptr,                         "config:automute, emitted when false"},
    {"--no-audio-processing", Arity::Flag, nullptr,                         "config:audioProcessing, emitted when false"},
    // Screenshot options
    {"--screenshot",         Arity::Value, nullptr,                         "direct: on-demand diagnostics only"},
    {"--screenshot-delay",   Arity::Value, nullptr,                         "direct: on-demand diagnostics only"},
    // Content options
    {"--assets-dir",         Arity::Value, nullptr,                         "config:assetsDir"},
    // Configuration options
    {"--disable-particles",  Arity::Flag,  nullptr,                         "config:disableParticles"},
    {"--disable-mouse",      Arity::Flag,  nullptr,                         "config:disableMouse"},
    {"--disable-parallax",   Arity::Flag,  nullptr,                         "config:disableParallax"},
    {"-l/--list-properties", Arity::Flag,  nullptr,                         "direct: the cli properties command"},
    {"--set-property",       Arity::Value, nullptr,                         "config:properties, repeatable, alias --property; k=v, bare key means boolean true"},
    // Debugging options
    {"-z/--dump-structure",  Arity::Flag,  nullptr,                         "direct: on-demand diagnostics only"},
    {"--render-debug",       Arity::Value, nullptr,                         "direct: on-demand diagnostics only, repeatable"},
};

// Contract lookup by any slash-separated token of the row's flag: Find("--fps")
// and Find("-f") both return the -f/--fps row. Null when unknown.
const OptionSpec* Find(const std::string& flag);

// ---- invocation ------------------------------------------------------------
//
// Runtime data. Defaults mirror the engine's own argparse default_value and
// settings initializer (ApplicationContext.h settings{}), because the engine's
// defaults are part of its model; the config layer's defaults are the
// controller's policy and may deliberately differ from these.

// One --screen-root group: the screen plus everything that binds to it.
// Empty scaling/clamp means "not emitted" — the engine then applies its own
// per-screen default.
struct ScreenBinding {
    std::string screen;
    std::string background;
    std::string scaling;
    std::string clamp;
};

struct Invocation {
    std::string assetsDir;
    std::vector<ScreenBinding> screens;
    // the positional background id: the engine's fallback for screens without
    // --bg, and the target of --list-properties
    std::string backgroundId;

    int  fps = 30;
    bool fullscreenPause = true;
    bool automute = true;
    bool audioProcessing = true;
    int  volume = 15;
    bool silent = false; // engine default: audio on; config's default differs by policy
    bool disableParticles = false;
    bool disableMouse = false;
    bool disableParallax = false;

    std::vector<std::pair<std::string, std::string>> properties; // key=value

    // direct-mode switches; the wallpaper service never sets these
    bool listProperties = false;
};

// ---- the two actions -------------------------------------------------------

struct Diagnostic {
    std::string flag;    // offending option; "" for invocation-level rules
    std::string problem; // human-readable, names the engine rule
};

// Mirrors the constraints the engine enforces while parsing: --scaling and
// --clamp choices, the one-background minimum, the 0-128 volume window, and
// '='-free property keys (the engine splits k=v at the first '=', so a key
// carrying '=' would silently target the wrong property). An empty result
// guarantees Emit's argv parses the way the invocation intends.
std::vector<Diagnostic> Validate(const Invocation& invocation);

// Flatten to engine arguments — without the program path: argv[0] belongs to
// the caller. The order is grammar, not taste: per-screen groups first (each
// --screen-root switches the binding target the following --bg/--scaling/
// --clamp attach to), then globals, then repeatable --set-property, then the
// positional id last.
std::vector<std::string> Emit(const Invocation& invocation);

} // namespace engine
