#include "engineunit.h"

#include "argvbuilder.h"
#include "systemd/unitbuilder.h"
#include "systemdunit.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <xcb/xcb.h>
#include <xcb/randr.h>

namespace {

// overridable for tests (WALLPAPER_ENGINE_UNIT=<name>)
QString unitNameFromEnv() {
    static const QString name = qEnvironmentVariable("WALLPAPER_ENGINE_UNIT", "linux-wallpaperengine");
    return name;
}

} // namespace

namespace EngineUnit {

QString unitName() { return unitNameFromEnv(); }

QString unitPath() {
    // deliberately $HOME-based (not XStandardPaths): the USER systemd manager
    // must see this exact file, so XDG overrides from test shells must not
    // relocate it. Tests isolate themselves by unit name instead.
    const QString home = qEnvironmentVariable("HOME");
    return home + "/.config/systemd/user/" + unitNameFromEnv() + ".service";
}

QString unitFileContent(const Config& config) {
    // systemd ExecStart quoting: escapeExecArg quotes arguments containing
    // whitespace and doubles "$"/"%" so systemd's substitution does not eat
    // them; unitBackgrounds() inverts exactly this escaping
    QStringList escapedArgs;
    for (const QString& arg : buildArgv(config))
        escapedArgs << SystemdLayer::escapeExecArg(arg);
    const QString exec = escapedArgs.join(' ');

    // persist the XAUTHORITY path this session actually uses: sddm/gdm keep
    // the cookie outside $HOME, and the engine's user unit would otherwise
    // fail to open the display. %h/.Xauthority stays the fallback.
    const QString xauthority = qEnvironmentVariable("XAUTHORITY");
    const QString authLine = xauthority.isEmpty()
        ? QStringLiteral("Environment=XAUTHORITY=%h/.Xauthority\n")
        : QStringLiteral("Environment=XAUTHORITY=%1\n")
              .arg(xauthority.contains(' ') ? '"' + xauthority + '"' : xauthority);

    return QStringLiteral(
        "[Unit]\n"
        "Description=linux-wallpaperengine dynamic wallpaper\n"
        "PartOf=graphical-session.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "Environment=DISPLAY=%1\n"
        "%2"
        "ExecStart=%3\n"
        "Restart=on-failure\n"
        "RestartSec=3\n"
        "\n"
        "[Install]\n"
        "WantedBy=graphical-session.target\n")
        .arg(config.display, authLine, exec);
}

QMap<QString, QString> unitBackgrounds() {
    QMap<QString, QString> result;
    QFile file(unitPath());
    if (!file.open(QIODevice::ReadOnly))
        return result;

    // the unit file is what systemd actually runs; parse its ExecStart so
    // status reflects reality even after manual unit edits. parseExecArgs
    // inverts the escaping unitFileContent applied.
    static const QString execKey = QStringLiteral("ExecStart=");
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    for (const QString& line : lines) {
        if (!line.startsWith(execKey))
            continue;

        const QStringList args = SystemdLayer::parseExecArgs(line.mid(execKey.size()));
        QString screen;
        for (int i = 0; i < args.size(); i++) {
            if (args.at(i) == "--screen-root" && i + 1 < args.size())
                screen = args.at(++i);
            else if (args.at(i) == "--bg" && i + 1 < args.size() && !screen.isEmpty())
                result.insert(screen, args.at(++i));
        }
        break; // exactly one ExecStart per generated unit
    }
    return result;
}

bool writeUnitFile(const Config& config) {
    const QString path = unitPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(unitFileContent(config).toUtf8());
    return true;
}

// every lifecycle operation goes through the typed D-Bus layer — the manager
// is addressed directly on the session bus, no systemctl subprocesses
bool daemonReload() {
    SystemdLayer::Error error;
    SystemdLayer::daemonReload(&error);
    return error.kind == SystemdLayer::Error::NoError;
}

bool startUnit() {
    SystemdLayer::SystemdUnit unit(unitNameFromEnv());
    SystemdLayer::Error error;
    unit.start(&error);
    return error.kind == SystemdLayer::Error::NoError;
}

bool restartUnit() {
    SystemdLayer::SystemdUnit unit(unitNameFromEnv());
    SystemdLayer::Error error;
    unit.restart(&error);
    return error.kind == SystemdLayer::Error::NoError;
}

bool stopUnit() {
    SystemdLayer::SystemdUnit unit(unitNameFromEnv());
    SystemdLayer::Error error;
    unit.stop(&error);
    // stop/reset-failed on a unit that was never loaded already has the
    // desired end state: systemd reports NoSuchUnit ("not loaded",
    // systemctl's old exit code 5). Treat it as success — keeps the CLI
    // idempotent for scripting.
    return SystemdLayer::tolerated(error);
}

QString unitState() {
    SystemdLayer::SystemdUnit unit(unitNameFromEnv());
    return unit.activeState();
}

QString fallbackScreenName() {
    // the engine renders on X11, so the name must come from the X server's
    // own RandR view — not from a frontend's QPA platform (a Wayland-session
    // GUI would report output names the engine cannot match). "DP-0" stays
    // the fallback for headless runs (no usable X server at all).
    QString name = QStringLiteral("DP-0");
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        return name;
    }

    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    if (screen != nullptr) {
        xcb_randr_get_output_primary_reply_t* primary = xcb_randr_get_output_primary_reply(
            connection, xcb_randr_get_output_primary(connection, screen->root), nullptr);
        if (primary != nullptr) {
            xcb_randr_get_output_info_reply_t* info = xcb_randr_get_output_info_reply (
                connection, xcb_randr_get_output_info(connection, primary->output, XCB_CURRENT_TIME), nullptr);
            // a connected, currently-driven output carries the authoritative name
            if (info != nullptr && info->crtc != XCB_NONE && info->name_len > 0)
                name = QString::fromLatin1(reinterpret_cast<const char*>(xcb_randr_get_output_info_name(info)),
                                            info->name_len);
            free(info);
            free(primary);
        }
    }
    xcb_disconnect(connection);
    return name;
}

void assignScreen(Config& config, const QString& wallpaperId) {
    if (config.screens.isEmpty())
        config.screens.insert(fallbackScreenName(), wallpaperId);
    else
        config.screens.begin().value() = wallpaperId; // single-screen v1
}

bool applyConfig(const Config& config) {
    // the one apply chain: persist the draft, project it into the unit file
    // the manager runs, reload, restart
    return config.save() && writeUnitFile(config) && daemonReload() && restartUnit();
}

} // namespace EngineUnit
