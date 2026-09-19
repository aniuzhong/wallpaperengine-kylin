#pragma once

#include <QStringList>

// Headless control surface. `wallpaper-engine <command> [options]` — the
// UI window is only entered when the binary is started without arguments.
// Returns the process exit code: 0 ok, 1 failure, 2 usage error.
int runCli (const QStringList& args);
