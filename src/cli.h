#pragma once

#include <string>
#include <vector>

// Headless control surface. `wallpaper-engine <command> [options]` never
// opens a window and works with no display at all (SSH, CI, pre-login).
// Returns the process exit code: 0 ok, 1 failure, 2 usage error.
int RunCli(const std::vector<std::string>& args);
