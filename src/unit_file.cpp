#include "unit_file.h"

#include "argvbuilder.h"
#include "engine.h"
#include "exec_args.h"
#include "posix.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <pwd.h>
#include <unistd.h>

namespace fs = std::filesystem;

inline std::string HomeDir() {
    const char* home = getenv("HOME");
    if (home != nullptr && *home != '\0')
        return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return pw->pw_dir;
    return {};
}

inline std::string SystemdUserUnit(const std::string& unit) {
    return HomeDir() + "/.config/systemd/user/" + unit + ".service";
}

namespace unit_file {

std::string Path(const std::string& unit) {
    return SystemdUserUnit(unit);
}

std::string Text(const config::Config& config, const std::string& xauthority) {
    // systemd ExecStart quoting: EscapeExecArg quotes arguments containing
    // whitespace and doubles "$"/"%" so systemd's substitution does not eat
    // them; Backgrounds() inverts exactly this escaping
    std::string exec;
    for (const std::string& arg : argvbuilder::BuildArgv(config)) {
        if (!exec.empty())
            exec += ' ';
        exec += systemd::EscapeExecArg(arg);
    }

    // persist the XAUTHORITY path this session actually uses: sddm/gdm keep
    // the cookie outside $HOME, and the engine's user unit would otherwise
    // fail to open the display. %h/.Xauthority stays the fallback.
    std::string authLine;
    if (xauthority.empty())
        authLine = "Environment=XAUTHORITY=%h/.Xauthority\n";
    else if (xauthority.find(' ') != std::string::npos)
        authLine = "Environment=XAUTHORITY=\"" + xauthority + "\"\n";
    else
        authLine = "Environment=XAUTHORITY=" + xauthority + "\n";

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

std::map<std::string, std::string> Backgrounds(const std::string& unit) {
    std::map<std::string, std::string> result;
    std::ifstream file(Path(unit));
    if (!file.is_open())
        return result;

    // the unit file is what systemd actually runs; parse its ExecStart so
    // status reflects reality even after manual unit edits. ParseExecArgs
    // inverts the escaping Text() applied.
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

        const std::vector<std::string> args = systemd::ParseExecArgs(line.substr(std::strlen(execKey)));
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

we::Result<void> Install(const config::Config& config, const std::string& unit) {
    // front-load the engine's constraints: an invocation the engine would
    // refuse must fail here, with the rule named, instead of installing a
    // unit that dies on start. One diagnostic per line in the message.
    const engine::Invocation invocation = argvbuilder::Project(config);
    const std::vector<engine::Diagnostic> problems = engine::Validate(invocation);
    if (!problems.empty()) {
        we::Error error;
        error.kind = we::Error::InvalidInput;
        for (const engine::Diagnostic& problem : problems) {
            if (!error.message.empty())
                error.message += "; ";
            error.message += problem.flag.empty() ? problem.problem : problem.flag + ": " + problem.problem;
        }
        return tl::unexpected(std::move(error));
    }

    const char* xauthority = getenv("XAUTHORITY");
    const std::string auth = (xauthority != nullptr && *xauthority != '\0') ? xauthority : "";

    const std::string path = Path(unit);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    // atomic like the config: systemd must never read a half-written unit
    return we::WriteFileAtomic(path, Text(config, auth));
}

} // namespace unit_file
