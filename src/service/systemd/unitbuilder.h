#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// Pure functions of the systemd layer: turn standard types (strings, argv,
// environment maps) into unit file text and StartTransientUnit property
// structures. No bus access, no wallpaper knowledge — fully unit-testable.
namespace SystemdLayer {

// Declarative definition of a simple service unit.
struct UnitDefinition {
    QString description;
    QStringList execArgs;                 // argv; execArgs[0] is the program
    QMap<QString, QString> environment;   // name -> value
    bool restartOnFailure = true;
    QString partOf = "graphical-session.target";
};

// systemd ExecStart entry: (path, argv, ignore-failure), i.e. type (sasb).
struct ExecCommand {
    QString program;
    QStringList args;
    bool ignoreFailure = false;
};

// Environment assignments as "NAME=VALUE" strings (systemd's `as` form).
QStringList environmentAssignments (const QMap<QString, QString>& environment);

// Decompose argv into an ExecStart command (program = argv[0]). A null
// command (empty program) is returned for empty argv.
ExecCommand toExecCommand (const QStringList& execArgs);

// Escape one ExecStart argument per systemd command-line rules: quoting for
// whitespace/quotes/semicolons/backslashes, "$$" for literal "$", "%%" for
// literal "%" (systemd applies environment and specifier substitution).
QString escapeExecArg (const QString& arg);

// Render the persistent unit file for a definition. Deterministic: the same
// definition always produces the same text.
QString buildUnitFile (const UnitDefinition& def);

} // namespace SystemdLayer
