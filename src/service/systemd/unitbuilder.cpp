#include "unitbuilder.h"

namespace SystemdLayer {

QString escapeExecArg (const QString& arg) {
    QString escaped = arg;
    // literal $ and % must be doubled: systemd substitutes $VAR/${VAR} and
    // %specifiers in ExecStart arguments
    escaped.replace ('$', "$$");
    escaped.replace ('%', "%%");
    bool needsQuoting = false;
    for (const char c : { ' ', '\t', '"', '\'', ';', '\\' }) {
        if (escaped.contains (c)) {
            needsQuoting = true;
            break;
        }
    }
    if (needsQuoting) {
        escaped.replace ('\\', "\\\\");
        escaped.replace ('"', "\\\"");
        escaped = '"' + escaped + '"';
    }
    return escaped;
}

QStringList environmentAssignments (const QMap<QString, QString>& environment) {
    QStringList assignments;
    for (auto it = environment.begin (); it != environment.end (); ++it)
        assignments << it.key () + "=" + it.value ();
    return assignments;
}

ExecCommand toExecCommand (const QStringList& execArgs) {
    ExecCommand command;
    if (!execArgs.isEmpty ()) {
        command.program = execArgs.first ();
        command.args = execArgs;
    }
    return command;
}

QString buildUnitFile (const UnitDefinition& def) {
    const ExecCommand command = toExecCommand (def.execArgs);
    QStringList escapedArgs;
    for (const QString& arg : command.args)
        escapedArgs << escapeExecArg (arg);

    QString text;
    text += "[Unit]\n";
    text += "Description=" + (def.description.isEmpty () ? QString ("wallpaper unit") : def.description) + "\n";
    if (!def.partOf.isEmpty ())
        text += "PartOf=" + def.partOf + "\n";
    text += "\n[Service]\n";
    text += "Type=simple\n";
    text += "ExecStart=" + escapedArgs.join (' ') + "\n";

    const QStringList assignments = environmentAssignments (def.environment);
    if (!assignments.isEmpty ()) {
        QStringList escaped;
        for (const QString& assignment : assignments)
            escaped << escapeExecArg (assignment);
        text += "Environment=" + escaped.join (' ') + "\n";
    }

    if (def.restartOnFailure)
        text += "Restart=on-failure\nRestartSec=3\n";

    text += "\n[Install]\nWantedBy=graphical-session.target\n";
    return text;
}

} // namespace SystemdLayer
