#include "engineunit.h"

#include "argvbuilder.h"
#include "systemdunit.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// overridable for tests (WALLPAPER_ENGINE_UNIT=<name>)
QString unitNameFromEnv () {
    static const QString name = qEnvironmentVariable ("WALLPAPER_ENGINE_UNIT", "linux-wallpaperengine");
    return name;
}

// stop/reset-failed on a unit that was never loaded already has the desired
// end state: systemd reports NoSuchUnit ("not loaded", systemctl's old exit
// code 5). Treat it as success — keeps the CLI idempotent for scripting.
// Mirrors the tolerance in SystemdLayer::resetFailed.
bool tolerated (const SystemdLayer::Error& error) {
    return error.kind == SystemdLayer::Error::NoError ||
           error.kind == SystemdLayer::Error::NoSuchUnit || error.message.contains ("not loaded");
}

} // namespace

namespace EngineUnit {

QString unitName () { return unitNameFromEnv (); }

QString unitPath () {
    // deliberately $HOME-based (not XStandardPaths): the USER systemd manager
    // must see this exact file, so XDG overrides from test shells must not
    // relocate it. Tests isolate themselves by unit name instead.
    const QString home = qEnvironmentVariable ("HOME");
    return home + "/.config/systemd/user/" + unitNameFromEnv () + ".service";
}

QString unitFileContent (const Config& config) {
    // systemd ExecStart quoting: double-quote arguments containing spaces
    QString exec;
    const QStringList argv = buildArgv (config);
    for (const QString& arg : argv) {
        if (!exec.isEmpty ())
            exec += QLatin1Char (' ');
        exec += (arg.contains (' ') || arg.contains ('"')) ? '"' + arg + '"' : arg;
    }

    // persist the XAUTHORITY path this session actually uses: sddm/gdm keep
    // the cookie outside $HOME, and the engine's user unit would otherwise
    // fail to open the display. %h/.Xauthority stays the fallback.
    const QString xauthority = qEnvironmentVariable ("XAUTHORITY");
    const QString authLine = xauthority.isEmpty ()
        ? QStringLiteral ("Environment=XAUTHORITY=%h/.Xauthority\n")
        : QStringLiteral ("Environment=XAUTHORITY=%1\n")
              .arg (xauthority.contains (' ') ? '"' + xauthority + '"' : xauthority);

    return QStringLiteral (
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
        .arg (config.display, authLine, exec);
}

QMap<QString, QString> unitBackgrounds () {
    QMap<QString, QString> result;
    QFile file (unitPath ());
    if (!file.open (QIODevice::ReadOnly))
        return result;

    // the unit file is what systemd actually runs; parse its ExecStart so
    // status reflects reality even after manual unit edits. Tokenizing
    // mirrors unitFileContent's quoting: arguments with spaces are wrapped
    // in double quotes, inner quotes are backslash-escaped.
    static const QString execKey = QStringLiteral ("ExecStart=");
    const QStringList lines = QString::fromUtf8 (file.readAll ()).split ('\n');
    for (const QString& line : lines) {
        if (!line.startsWith (execKey))
            continue;

        const QString body = line.mid (execKey.size ());
        QStringList args;
        QString current;
        bool inQuotes = false;
        for (int i = 0; i < body.size (); i++) {
            const QChar c = body.at (i);
            if (inQuotes && c == '\\' && i + 1 < body.size () && body.at (i + 1) == '"') {
                current += '"';
                i++;
            } else if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ' ' && !inQuotes) {
                if (!current.isEmpty ())
                    args << current;
                current.clear ();
            } else {
                current += c;
            }
        }
        if (!current.isEmpty ())
            args << current;

        QString screen;
        for (int i = 0; i < args.size (); i++) {
            if (args.at (i) == "--screen-root" && i + 1 < args.size ())
                screen = args.at (++i);
            else if (args.at (i) == "--bg" && i + 1 < args.size () && !screen.isEmpty ())
                result.insert (screen, args.at (++i));
        }
        break; // exactly one ExecStart per generated unit
    }
    return result;
}

bool writeUnitFile (const Config& config) {
    const QString path = unitPath ();
    QDir ().mkpath (QFileInfo (path).absolutePath ());
    QFile file (path);
    if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write (unitFileContent (config).toUtf8 ());
    return true;
}

// every lifecycle operation goes through the typed D-Bus layer — the manager
// is addressed directly on the session bus, no systemctl subprocesses
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
    return tolerated (error);
}

QString unitState () {
    SystemdLayer::SystemdUnit unit (unitNameFromEnv ());
    return unit.activeState ();
}

} // namespace EngineUnit
