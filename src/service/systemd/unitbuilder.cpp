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

QStringList parseExecArgs (const QString& line) {
    QStringList args;
    QString current;
    bool inQuotes = false;
    const auto flush = [&] {
        if (!current.isEmpty ()) {
            args << current;
            current.clear ();
        }
    };

    for (int i = 0; i < line.size (); i++) {
        const QChar c = line.at (i);
        if (inQuotes) {
            // escapeExecArg only emits \\ and \" inside quotes; any other
            // backslash sequence stays literal
            if (c == '\\' && i + 1 < line.size () && (line.at (i + 1) == '"' || line.at (i + 1) == '\\')) {
                current += line.at (i + 1);
                i++;
            } else if (c == '"') {
                inQuotes = false;
            } else {
                current += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ' ') {
            flush ();
        } else {
            current += c;
        }
    }
    flush ();

    // undo the doubling the same way systemd does before exec, so callers
    // see the argv the engine will actually run with
    for (QString& arg : args) {
        arg.replace ("$$", "$");
        arg.replace ("%%", "%");
    }
    return args;
}

ExecCommand toExecCommand (const QStringList& execArgs) {
    ExecCommand command;
    if (!execArgs.isEmpty ()) {
        command.program = execArgs.first ();
        command.args = execArgs;
    }
    return command;
}

} // namespace SystemdLayer
