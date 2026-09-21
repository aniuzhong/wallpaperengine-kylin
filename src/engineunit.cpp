#include "engineunit.h"

#include "argvbuilder.h"
#include "posix.h"
#include "unitbuilder.h"
#include "systemdunit.h"

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

// overridable for tests (WALLPAPER_ENGINE_UNIT=<name>)
std::string unitNameFromEnv() {
    static const std::string name = wallpaper_engine::envOr("WALLPAPER_ENGINE_UNIT", "wallpaper-engine");
    return name;
}

// The output RandR calls primary, when it is actually driven. Empty when the
// server has no opinion or the primary is disconnected.
std::string primaryOutputName(xcb_connection_t* connection, const xcb_screen_t* screen) {
    if (screen == nullptr)
        return {};
    std::string name;
    xcb_randr_get_output_primary_reply_t* primary = xcb_randr_get_output_primary_reply(
        connection, xcb_randr_get_output_primary(connection, screen->root), nullptr);
    if (primary != nullptr) {
        xcb_randr_get_output_info_reply_t* info = xcb_randr_get_output_info_reply(
            connection, xcb_randr_get_output_info(connection, primary->output, XCB_CURRENT_TIME), nullptr);
        // a connected, currently-driven output carries the authoritative name
        if (info != nullptr && info->crtc != XCB_NONE && info->name_len > 0)
            name.assign(reinterpret_cast<const char*> (xcb_randr_get_output_info_name(info)), info->name_len);
        free(info);
        free(primary);
    }
    return name;
}

// Every output with a crtc: an unconnected connector has none, and the
// engine cannot render where there is no crtc.
std::vector<std::string> drivenOutputs(xcb_connection_t* connection, const xcb_screen_t* screen) {
    std::vector<std::string> names;
    if (screen == nullptr)
        return names;

    xcb_randr_get_screen_resources_current_reply_t* resources = xcb_randr_get_screen_resources_current_reply(
        connection, xcb_randr_get_screen_resources_current(connection, screen->root), nullptr);
    if (resources == nullptr)
        return names;

    const xcb_randr_output_t* outputs = xcb_randr_get_screen_resources_current_outputs(resources);
    const int count = xcb_randr_get_screen_resources_current_outputs_length(resources);
    for (int i = 0; i < count; i++) {
        xcb_randr_get_output_info_reply_t* info = xcb_randr_get_output_info_reply(
            connection, xcb_randr_get_output_info(connection, outputs[i], XCB_CURRENT_TIME), nullptr);
        if (info != nullptr && info->crtc != XCB_NONE && info->name_len > 0)
            names.push_back(std::string(
                reinterpret_cast<const char*> (xcb_randr_get_output_info_name(info)), info->name_len));
        free(info);
    }
    free(resources);
    return names;
}

} // namespace

namespace EngineUnit {

std::string unitName() { return unitNameFromEnv(); }

std::string unitPath() {
    // deliberately $HOME-based (not XDG): the USER systemd manager must see
    // this exact file, so XDG overrides from test shells must not relocate
    // it. Tests isolate themselves by unit name instead.
    return wallpaper_engine::homeDir() + "/.config/systemd/user/" + unitNameFromEnv() + ".service";
}

std::string unitFileContent(const Config& config) {
    // systemd ExecStart quoting: escapeExecArg quotes arguments containing
    // whitespace and doubles "$"/"%" so systemd's substitution does not eat
    // them; unitBackgrounds() inverts exactly this escaping
    std::string exec;
    for (const std::string& arg : buildArgv(config)) {
        if (!exec.empty())
            exec += ' ';
        exec += systemd::escapeExecArg(arg);
    }

    // persist the XAUTHORITY path this session actually uses: sddm/gdm keep
    // the cookie outside $HOME, and the engine's user unit would otherwise
    // fail to open the display. %h/.Xauthority stays the fallback.
    const char* xauthority = getenv("XAUTHORITY");
    std::string authLine;
    if (xauthority == nullptr || *xauthority == '\0')
        authLine = "Environment=XAUTHORITY=%h/.Xauthority\n";
    else if (std::strchr(xauthority, ' ') != nullptr)
        authLine = std::string("Environment=XAUTHORITY=\"") + xauthority + "\"\n";
    else
        authLine = std::string("Environment=XAUTHORITY=") + xauthority + "\n";

    return "[Unit]\n"
           "Description=wallpaper-engine dynamic wallpaper\n"
           "PartOf=graphical-session.target\n"
           "\n"
           "[Service]\n"
           "Type=simple\n"
           "Environment=DISPLAY=" +
           config.display + "\n" + authLine +
           "ExecStart=" + exec + "\n"
           "Restart=on-failure\n"
           "RestartSec=3\n"
           "\n"
           "[Install]\n"
           "WantedBy=graphical-session.target\n";
}

std::map<std::string, std::string> unitBackgrounds() {
    std::map<std::string, std::string> result;
    std::ifstream file(unitPath());
    if (!file.is_open())
        return result;

    // the unit file is what systemd actually runs; parse its ExecStart so
    // status reflects reality even after manual unit edits. parseExecArgs
    // inverts the escaping unitFileContent applied.
    std::string content { std::istreambuf_iterator<char> (file), std::istreambuf_iterator<char> () };
    constexpr const char* execKey = "ExecStart=";
    size_t lineStart = 0;
    while (lineStart <= content.size()) {
        size_t lineEnd = content.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = content.size();
        const std::string line = content.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        if (line.rfind(execKey, 0) != 0)
            continue;

        const std::vector<std::string> args = systemd::parseExecArgs(line.substr(std::strlen(execKey)));
        std::string screen;
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i] == "--screen-root" && i + 1 < args.size())
                screen = args[++i];
            else if (args[i] == "--bg" && i + 1 < args.size() && !screen.empty())
                result[screen] = args[++i];
        }
        break; // exactly one ExecStart per generated unit
    }
    return result;
}

bool writeUnitFile(const Config& config, wallpaper_engine::Error* error) {
    const std::string path = unitPath();
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    // atomic like the config: systemd must never read a half-written unit
    return wallpaper_engine::writeFileAtomic(path, unitFileContent(config), error);
}

// every lifecycle operation goes through the typed sd-bus layer — the
// manager is addressed directly on the session bus, no systemctl subprocesses
bool daemonReload(wallpaper_engine::Error* error) {
    wallpaper_engine::Error local;
    systemd::daemonReload(&local);
    if (error != nullptr)
        *error = local;
    return local.ok();
}

bool startUnit(wallpaper_engine::Error* error) {
    systemd::SystemdUnit unit(unitNameFromEnv());
    wallpaper_engine::Error local;
    unit.start(&local);
    if (error != nullptr)
        *error = local;
    return local.ok();
}

bool restartUnit(wallpaper_engine::Error* error) {
    systemd::SystemdUnit unit(unitNameFromEnv());
    wallpaper_engine::Error local;
    unit.restart(&local);
    if (error != nullptr)
        *error = local;
    return local.ok();
}

bool stopUnit(wallpaper_engine::Error* error) {
    systemd::SystemdUnit unit(unitNameFromEnv());
    wallpaper_engine::Error local;
    unit.stop(&local);
    // stop/reset-failed on a unit that was never loaded already has the
    // desired end state: systemd reports NoSuchUnit ("not loaded",
    // systemctl's old exit code 5). Treat it as success — keeps the CLI
    // idempotent for scripting.
    if (systemd::tolerated(local))
        local = {};
    if (error != nullptr)
        *error = local;
    return local.ok();
}

std::string unitState(wallpaper_engine::Error* error) {
    systemd::SystemdUnit unit(unitNameFromEnv());
    return unit.activeState(error);
}

std::string fallbackScreenName() {
    // the engine renders on X11, so the name must come from the X server's
    // own RandR view — not from a frontend's QPA platform (a Wayland-session
    // GUI would report output names the engine cannot match). "DP-0" stays
    // the fallback for headless runs (no usable X server at all).
    std::string name = "DP-0";
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        return name;
    }

    const std::string primary =
        primaryOutputName(connection, xcb_setup_roots_iterator(xcb_get_setup(connection)).data);
    if (!primary.empty())
        name = primary;
    xcb_disconnect(connection);
    return name;
}

std::vector<std::string> screenNames() {
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        return { "DP-0" };
    }

    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    std::vector<std::string> names = drivenOutputs(connection, screen);
    const std::string primary = primaryOutputName(connection, screen);
    xcb_disconnect(connection);

    if (names.empty())
        return { "DP-0" };
    // primary first: a caller that just takes front() gets the desktop's own
    // idea of the main screen, which is also what defaultScreenFor prefers
    const auto it = std::find(names.begin(), names.end(), primary);
    if (it != names.end())
        std::rotate(names.begin(), it, it + 1);
    return names;
}

std::string defaultScreenFor(const Config& config, const std::string& primaryOutput) {
    if (config.screens.count(primaryOutput) != 0)
        return primaryOutput; // the desktop already drives it
    if (config.screens.size() == 1)
        return config.screens.begin()->first; // the only candidate there is
    return primaryOutput; // fresh config, or a multi-screen one without the primary
}

Config assignScreen(Config config, const std::string& screen, const std::string& wallpaperId) {
    // a screens[""] entry would be unmatchable by the engine: leave the
    // config untouched rather than write one
    if (screen.empty() || wallpaperId.empty())
        return config;
    config.screens[screen] = wallpaperId;
    return config;
}

bool applyConfig(const Config& config, wallpaper_engine::Error* error) {
    // the one apply chain: persist the draft, project it into the unit file
    // the manager runs, reload, restart. The first failing step is the one
    // |error| describes.
    wallpaper_engine::Error local;
    const bool ok = config.save(&local) && writeUnitFile(config, &local) && daemonReload(&local) &&
                    restartUnit(&local);
    if (error != nullptr)
        *error = ok ? wallpaper_engine::Error {} : local;
    return ok;
}

} // namespace EngineUnit
