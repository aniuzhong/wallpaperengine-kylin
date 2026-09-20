#include "uilaunch.h"

#include "posix.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include <sys/wait.h>
#include <unistd.h>

namespace Ui {

namespace {

// Every directory a .desktop file may live in, in the order the desktop
// search path puts them: the user's own first, then XDG_DATA_DIRS (which on
// a Flatpak-using desktop includes the exports directories — that is what
// makes the Flatpak browser resolvable at all).
std::vector<std::string> dataDirs() {
    std::vector<std::string> dirs;
    dirs.push_back(lwe::envOr("XDG_DATA_HOME", lwe::homeDir() + "/.local/share"));

    const std::string fromEnv = lwe::envOr("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
    size_t start = 0;
    while (start <= fromEnv.size()) {
        const size_t end = fromEnv.find(':', start);
        const size_t stop = end == std::string::npos ? fromEnv.size() : end;
        if (stop > start)
            dirs.push_back(fromEnv.substr(start, stop - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return dirs;
}

// Run a fixed command and hand back its stdout. Only used for xdg-settings,
// whose argv is a compile-time constant — the quoting is for correctness,
// not for untrusted input.
std::string runForOutput(const std::vector<std::string>& argv) {
    std::string command;
    for (const std::string& arg : argv) {
        if (!command.empty())
            command += ' ';
        command += '\'';
        for (const char c : arg) {
            if (c == '\'')
                command += "'\\''";
            else
                command += c;
        }
        command += '\'';
    }

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
        return {};
    std::string output;
    char buffer[256];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0)
        output.append(buffer, n);
    pclose(pipe);
    return output;
}

// Remove the %-field codes from one argument. "%%" is a literal percent;
// the field codes themselves expand to something the caller already knows
// (argv[0] is the program, the URL is appended separately).
std::string stripFieldCodes(const std::string& token) {
    std::string out;
    for (size_t i = 0; i < token.size(); i++) {
        if (token[i] != '%' || i + 1 >= token.size()) {
            out += token[i];
            continue;
        }
        const char code = token[i + 1];
        if (code == '%') {
            out += '%';
            i++;
        } else if (std::strchr("uUfFiIcKk", code) != nullptr) {
            i++; // dropped
        } else {
            out += token[i];
        }
    }
    return out;
}

} // namespace

std::string parseDesktopExec(const std::string& desktopFileContents) {
    bool inMainGroup = false;
    size_t start = 0;
    while (start <= desktopFileContents.size()) {
        const size_t end = desktopFileContents.find('\n', start);
        const size_t stop = end == std::string::npos ? desktopFileContents.size() : end;
        std::string line = desktopFileContents.substr(start, stop - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (!line.empty() && line[0] == '[')
            inMainGroup = line == "[Desktop Entry]";
        else if (inMainGroup && line.rfind("Exec=", 0) == 0)
            return line.substr(std::strlen("Exec="));

        if (end == std::string::npos)
            break;
        start = stop + 1;
    }
    return {};
}

std::vector<std::string> parseExecLine(const std::string& execLine) {
    std::vector<std::string> tokens;
    std::string current;
    bool started = false; // an explicit "" is an empty argument, not no argument
    bool inQuotes = false;

    for (size_t i = 0; i < execLine.size(); i++) {
        const char c = execLine[i];
        if (inQuotes) {
            if (c == '\\' && i + 1 < execLine.size()) {
                const char next = execLine[i + 1];
                // the spec reserves these four escapes inside quotes;
                // anything else keeps its backslash
                if (next == '"' || next == '\\' || next == '`' || next == '$') {
                    current += next;
                    i++;
                    continue;
                }
            }
            if (c == '"') {
                inQuotes = false;
                continue;
            }
            current += c;
            continue;
        }

        if (c == '"') {
            inQuotes = true;
            started = true;
        } else if (c == ' ' || c == '\t') {
            if (started) {
                tokens.push_back(current);
                current.clear();
                started = false;
            }
        } else {
            current += c;
            started = true;
        }
    }
    if (started)
        tokens.push_back(current);
    return tokens;
}

bool isChromiumFamily(const std::string& execLine) {
    std::string lower;
    lower.reserve(execLine.size());
    for (const char c : execLine)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const char* marker : { "chrom", "brave", "microsoft-edge", "msedge", "vivaldi", "opera",
                                "qaxbrowser", "com.google.chrome", "360browser", "browser360", "yandex" })
        if (lower.find(marker) != std::string::npos)
            return true;
    return false;
}

Launch launchFor(const std::string& execLine, const std::string& url) {
    Launch launch;
    for (const std::string& token : parseExecLine(execLine)) {
        const std::string cleaned = stripFieldCodes(token);
        if (!cleaned.empty())
            launch.argv.push_back(cleaned);
    }
    if (launch.argv.empty())
        return launch;

    if (isChromiumFamily(execLine)) {
        launch.argv.push_back("--app=" + url);
        launch.appWindow = true;
    } else {
        launch.argv.push_back(url);
    }
    return launch;
}

std::string defaultBrowserExec() {
    std::string id = runForOutput({ "xdg-settings", "get", "default-web-browser" });
    // xdg-settings answers with a trailing newline, and with a usage message
    // on failure
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' '))
        id.pop_back();
    if (id.empty() || id.find(' ') != std::string::npos || id.find('/') != std::string::npos)
        return {};

    for (const std::string& dir : dataDirs()) {
        std::ifstream file(dir + "/applications/" + id);
        if (!file.is_open())
            continue;
        const std::string contents { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        const std::string exec = parseDesktopExec(contents);
        if (!exec.empty())
            return exec;
    }
    return {};
}

bool openBrowser(const std::string& url, std::string* error) {
    const std::string exec = defaultBrowserExec();
    if (!exec.empty() && lwe::spawnDetached(launchFor(exec, url).argv))
        return true;

    // the desktop's own opener: whatever association we failed to resolve,
    // this is the thing that would have resolved it
    if (lwe::spawnDetached({ "xdg-open", url }))
        return true;

    if (error != nullptr)
        *error = exec.empty() ? "no default browser is configured"
                              : "could not start the default browser";
    return false;
}

} // namespace Ui
