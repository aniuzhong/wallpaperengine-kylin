#include "engineunit.h"

#include "argvbuilder.h"
#include "posix.h"
#include "systemd/unitbuilder.h"
#include "systemdunit.h"

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// overridable for tests (WALLPAPER_ENGINE_UNIT=<name>)
std::string unitNameFromEnv () {
    static const std::string name = lwe::envOr ("WALLPAPER_ENGINE_UNIT", "linux-wallpaperengine");
    return name;
}

} // namespace

namespace EngineUnit {

std::string unitName () { return unitNameFromEnv (); }

std::string unitPath () {
    // deliberately $HOME-based (not XDG): the USER systemd manager must see
    // this exact file, so XDG overrides from test shells must not relocate
    // it. Tests isolate themselves by unit name instead.
    return lwe::homeDir () + "/.config/systemd/user/" + unitNameFromEnv () + ".service";
}

std::string unitFileContent (const Config& config) {
    // systemd ExecStart quoting: escapeExecArg quotes arguments containing
    // whitespace and doubles "$"/"%" so systemd's substitution does not eat
    // them; unitBackgrounds() inverts exactly this escaping
    std::string exec;
    for (const std::string& arg : buildArgv (config)) {
        if (!exec.empty ())
            exec += ' ';
        exec += SystemdLayer::escapeExecArg (arg);
    }

    // persist the XAUTHORITY path this session actually uses: sddm/gdm keep
    // the cookie outside $HOME, and the engine's user unit would otherwise
    // fail to open the display. %h/.Xauthority stays the fallback.
    const char* xauthority = getenv ("XAUTHORITY");
    std::string authLine;
    if (xauthority == nullptr || *xauthority == '\0')
        authLine = "Environment=XAUTHORITY=%h/.Xauthority\n";
    else if (std::strchr (xauthority, ' ') != nullptr)
        authLine = std::string ("Environment=XAUTHORITY=\"") + xauthority + "\"\n";
    else
        authLine = std::string ("Environment=XAUTHORITY=") + xauthority + "\n";

    return "[Unit]\n"
           "Description=linux-wallpaperengine dynamic wallpaper\n"
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

std::map<std::string, std::string> unitBackgrounds () {
    std::map<std::string, std::string> result;
    std::ifstream file (unitPath ());
    if (!file.is_open ())
        return result;

    // the unit file is what systemd actually runs; parse its ExecStart so
    // status reflects reality even after manual unit edits. parseExecArgs
    // inverts the escaping unitFileContent applied.
    std::string content { std::istreambuf_iterator<char> (file), std::istreambuf_iterator<char> () };
    constexpr const char* execKey = "ExecStart=";
    size_t lineStart = 0;
    while (lineStart <= content.size ()) {
        size_t lineEnd = content.find ('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = content.size ();
        const std::string line = content.substr (lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        if (line.rfind (execKey, 0) != 0)
            continue;

        const std::vector<std::string> args = SystemdLayer::parseExecArgs (line.substr (std::strlen (execKey)));
        std::string screen;
        for (size_t i = 0; i < args.size (); i++) {
            if (args[i] == "--screen-root" && i + 1 < args.size ())
                screen = args[++i];
            else if (args[i] == "--bg" && i + 1 < args.size () && !screen.empty ())
                result[screen] = args[++i];
        }
        break; // exactly one ExecStart per generated unit
    }
    return result;
}

bool writeUnitFile (const Config& config) {
    const std::string path = unitPath ();
    std::error_code ec;
    fs::create_directories (fs::path (path).parent_path (), ec);
    std::ofstream file (path, std::ios::trunc);
    if (!file.is_open ())
        return false;
    file << unitFileContent (config);
    return file.good ();
}

// every lifecycle operation goes through the typed sd-bus layer — the
// manager is addressed directly on the session bus, no systemctl subprocesses
bool daemonReload () {
    SystemdLayer::Error error;
    SystemdLayer::daemonReload (&error);
    return error.kind == SystemdLayer::Error::NoError;
}

bool startUnit () {
    SystemdLayer::SystemdUnit unit (unitNameFromEnv ());
    SystemdLayer::Error error;
    unit.start (&error);
    return error.kind == SystemdLayer::Error::NoError;
}

bool restartUnit () {
    SystemdLayer::SystemdUnit unit (unitNameFromEnv ());
    SystemdLayer::Error error;
    unit.restart (&error);
    return error.kind == SystemdLayer::Error::NoError;
}

bool stopUnit () {
    SystemdLayer::SystemdUnit unit (unitNameFromEnv ());
    SystemdLayer::Error error;
    unit.stop (&error);
    // stop/reset-failed on a unit that was never loaded already has the
    // desired end state: systemd reports NoSuchUnit ("not loaded",
    // systemctl's old exit code 5). Treat it as success — keeps the CLI
    // idempotent for scripting.
    return SystemdLayer::tolerated (error);
}

std::string unitState () {
    SystemdLayer::SystemdUnit unit (unitNameFromEnv ());
    return unit.activeState ();
}

std::string fallbackScreenName () {
    // the engine renders on X11, so the name must come from the X server's
    // own RandR view — not from a frontend's QPA platform (a Wayland-session
    // GUI would report output names the engine cannot match). "DP-0" stays
    // the fallback for headless runs (no usable X server at all).
    std::string name = "DP-0";
    xcb_connection_t* connection = xcb_connect (nullptr, nullptr);
    if (xcb_connection_has_error (connection)) {
        xcb_disconnect (connection);
        return name;
    }

    const xcb_screen_t* screen = xcb_setup_roots_iterator (xcb_get_setup (connection)).data;
    if (screen != nullptr) {
        xcb_randr_get_output_primary_reply_t* primary = xcb_randr_get_output_primary_reply (
            connection, xcb_randr_get_output_primary (connection, screen->root), nullptr);
        if (primary != nullptr) {
            xcb_randr_get_output_info_reply_t* info = xcb_randr_get_output_info_reply (
                connection, xcb_randr_get_output_info (connection, primary->output, XCB_CURRENT_TIME), nullptr);
            // a connected, currently-driven output carries the authoritative name
            if (info != nullptr && info->crtc != XCB_NONE && info->name_len > 0)
                name.assign (reinterpret_cast<const char*> (xcb_randr_get_output_info_name (info)), info->name_len);
            free (info);
            free (primary);
        }
    }
    xcb_disconnect (connection);
    return name;
}

void assignScreen (Config& config, const std::string& wallpaperId) {
    if (config.screens.empty ())
        config.screens[fallbackScreenName ()] = wallpaperId;
    else
        config.screens.begin ()->second = wallpaperId; // single-screen v1
}

bool applyConfig (const Config& config) {
    // the one apply chain: persist the draft, project it into the unit file
    // the manager runs, reload, restart
    return config.save () && writeUnitFile (config) && daemonReload () && restartUnit ();
}

} // namespace EngineUnit
