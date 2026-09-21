#include "unitbuilder.h"

namespace SystemdLayer {

std::string escapeExecArg(const std::string& arg) {
    // literal $ and % must be doubled: systemd substitutes $VAR/${VAR} and
    // %specifiers in ExecStart arguments
    std::string escaped;
    escaped.reserve(arg.size());
    for (const char c : arg) {
        if (c == '$' || c == '%') {
            escaped += c;
            escaped += c;
        } else {
            escaped += c;
        }
    }

    bool needsQuoting = false;
    for (const char c : escaped) {
        if (c == ' ' || c == '\t' || c == '"' || c == '\'' || c == ';' || c == '\\') {
            needsQuoting = true;
            break;
        }
    }
    if (!needsQuoting)
        return escaped;

    std::string quoted = "\"";
    for (const char c : escaped) {
        if (c == '\\' || c == '"')
            quoted += '\\';
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

std::vector<std::string> parseExecArgs(const std::string& line) {
    std::vector<std::string> args;
    std::string current;
    bool inQuotes = false;
    const auto flush = [&] {
        if (!current.empty()) {
            args.push_back(current);
            current.clear();
        }
    };

    for (size_t i = 0; i < line.size(); i++) {
        const char c = line[i];
        if (inQuotes) {
            // escapeExecArg only emits \\ and \" inside quotes; any other
            // backslash sequence stays literal
            if (c == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) {
                current += line[i + 1];
                i++;
            } else if (c == '"') {
                inQuotes = false;
            } else {
                current += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ' ') {
            flush();
        } else {
            current += c;
        }
    }
    flush();

    // undo the doubling the same way systemd does before exec, so callers
    // see the argv the engine will actually run with
    std::string doubled;
    for (std::string& arg : args) {
        doubled.clear();
        doubled.reserve(arg.size());
        for (size_t i = 0; i < arg.size(); i++) {
            doubled += arg[i];
            if ((arg[i] == '$' || arg[i] == '%') && i + 1 < arg.size() && arg[i + 1] == arg[i])
                i++; // skip the second half of $$ / %%
        }
        arg.swap(doubled);
    }
    return args;
}

ExecCommand toExecCommand(const std::vector<std::string>& execArgs) {
    ExecCommand command;
    if (!execArgs.empty()) {
        command.program = execArgs.front();
        command.args = execArgs;
    }
    return command;
}

} // namespace SystemdLayer
