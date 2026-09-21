#pragma once

#include <string>
#include <vector>

// Pure functions of the systemd layer: unit-id normalization, ExecStart
// text escaping/parsing, and transient property decomposition. No bus
// access, no wallpaper knowledge — fully unit-testable.
namespace systemd {

// Accept a bare name the way systemctl does: the manager requires a full
// unit id with the type suffix, so "wallpaper-engine" becomes
// "wallpaper-engine.service". A name that already carries a dot passes
// through untouched.
std::string CanonicalUnitName(const std::string& name);

// systemd ExecStart entry: (path, argv, ignore-failure), i.e. type (sasb).
struct ExecCommand {
    std::string program;
    std::vector<std::string> args;
    bool ignoreFailure = false;
};

// Decompose argv into an ExecStart command (program = argv[0]). A null
// command (empty program) is returned for empty argv.
ExecCommand ToExecCommand(const std::vector<std::string>& execArgs);

// Escape one ExecStart argument per systemd command-line rules: quoting for
// whitespace/quotes/semicolons/backslashes, "$$" for literal "$", "%%" for
// literal "%" (systemd applies environment and specifier substitution).
std::string EscapeExecArg(const std::string& arg);

// Inverse of EscapeExecArg over a joined ExecStart line: splits on unquoted
// spaces, undoes the in-quote backslash escapes and the doubled "$$"/"%%"
// (systemd undoes those before exec, so the result is the argv the process
// actually sees). An empty argument cannot be represented in ExecStart text
// and is dropped.
std::vector<std::string> ParseExecArgs(const std::string& line);

} // namespace systemd
