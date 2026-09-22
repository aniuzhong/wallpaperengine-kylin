#include "engine.h"

#include <string_view>

namespace engine {

namespace {

// contract choices are "|"-separated
bool oneOf(const char* choices, const std::string& value) {
    if (choices == nullptr)
        return true;
    const std::string_view rest(choices);
    size_t start = 0;
    while (true) {
        const size_t bar = rest.find('|', start);
        const std::string_view token = rest.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start);
        if (token == value)
            return true;
        if (bar == std::string_view::npos)
            return false;
        start = bar + 1;
    }
}

} // namespace

const OptionSpec* Find(const std::string& flag) {
    for (const OptionSpec& option : kOptions) {
        // rows store slash-combined names ("-f/--fps"); every token matches
        const std::string_view name = option.flag;
        size_t start = 0;
        while (start <= name.size()) {
            const size_t slash = name.find('/', start);
            const std::string_view token =
                name.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
            if (token == flag)
                return &option;
            if (slash == std::string_view::npos)
                break;
            start = slash + 1;
        }
    }
    return nullptr;
}

std::vector<Diagnostic> Validate(const Invocation& invocation) {
    std::vector<Diagnostic> problems;

    const OptionSpec* scaling = Find("--scaling");
    const OptionSpec* clamp = Find("--clamp");

    bool anyBackground = !invocation.backgroundId.empty();
    for (const ScreenBinding& screen : invocation.screens) {
        if (screen.screen.empty())
            problems.push_back({"--screen-root", "screen name must not be empty"});
        if (screen.background.empty())
            problems.push_back({"--bg", "background for screen '" + screen.screen + "' must not be empty"});
        anyBackground = anyBackground || !screen.background.empty();

        if (!screen.scaling.empty() && !oneOf(scaling->choices, screen.scaling))
            problems.push_back({"--scaling", "'" + screen.scaling + "' is not one of " + scaling->choices});
        if (!screen.clamp.empty() && !oneOf(clamp->choices, screen.clamp))
            problems.push_back({"--clamp", "'" + screen.clamp + "' is not one of " + clamp->choices});
    }
    // the engine refuses to start without any background at all
    if (!anyBackground)
        problems.push_back({"", "at least one background id must be specified"});

    // the engine parses any int, but <= 0 renders a frozen frame — fail at
    // the entry instead of a live-but-motionless unit
    if (invocation.fps < 1)
        problems.push_back({"--fps", "must be >= 1, got " + std::to_string(invocation.fps)});

    // the engine clamps silently into 0..128; surfacing here turns a
    // silently-wrong volume into a named error at start time
    if (invocation.volume < 0 || invocation.volume > 128)
        problems.push_back({"--volume", "must be in 0..128, got " + std::to_string(invocation.volume)});

    for (const auto& [key, value] : invocation.properties) {
        if (key.empty())
            problems.push_back({"--set-property", "property key must not be empty"});
        else if (key.find('=') != std::string::npos)
            problems.push_back({"--set-property", "property key '" + key + "' must not contain '='"});
    }

    return problems;
}

std::vector<std::string> Emit(const Invocation& invocation) {
    std::vector<std::string> args;

    if (!invocation.assetsDir.empty()) {
        args.push_back("--assets-dir");
        args.push_back(invocation.assetsDir);
    }

    // per-screen groups: --screen-root switches the binding target and the
    // following --bg/--scaling/--clamp attach to it — this order is the
    // engine's binding grammar, not a layout preference
    for (const ScreenBinding& screen : invocation.screens) {
        args.push_back("--screen-root");
        args.push_back(screen.screen);
        args.push_back("--bg");
        args.push_back(screen.background);
        if (!screen.scaling.empty()) {
            args.push_back("--scaling");
            args.push_back(screen.scaling);
        }
        if (!screen.clamp.empty()) {
            args.push_back("--clamp");
            args.push_back(screen.clamp);
        }
    }

    // full explicit emission: the systemd unit file stays self-documenting,
    // status/doctor can read the engine's complete picture from ExecStart
    args.push_back("--fps");
    args.push_back(std::to_string(invocation.fps));
    if (!invocation.fullscreenPause)
        args.push_back("--no-fullscreen-pause");
    if (!invocation.automute)
        args.push_back("--noautomute");
    if (!invocation.audioProcessing)
        args.push_back("--no-audio-processing");

    // mutually exclusive pair in the engine's argparse; --silent wins
    if (invocation.silent) {
        args.push_back("--silent");
    } else {
        args.push_back("--volume");
        args.push_back(std::to_string(invocation.volume));
    }

    if (invocation.disableParticles)
        args.push_back("--disable-particles");
    if (invocation.disableMouse)
        args.push_back("--disable-mouse");
    if (invocation.disableParallax)
        args.push_back("--disable-parallax");

    for (const auto& [key, value] : invocation.properties) {
        args.push_back("--set-property");
        args.push_back(key + "=" + value);
    }

    if (invocation.listProperties)
        args.push_back("--list-properties");

    // the positional id goes last: argparse accepts it interleaved, this is
    // the documented shape
    if (!invocation.backgroundId.empty())
        args.push_back(invocation.backgroundId);

    return args;
}

} // namespace engine
