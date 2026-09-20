#pragma once

#include <map>
#include <string>

// Pure half of the peony integration — the same split as
// unitbuilder/systemdunit in the systemd layer. Everything here is a plain
// function of its arguments, so the fiddly parts (the wallpaper candidate
// list, the shim environment) are unit-testable; the syscall sequence that
// consumes them lives in integration.cpp.
namespace Integration {

// True when a /proc/<pid>/cmdline is the peony desktop process. The uid
// check stays with the caller: another session's peony is not ours to touch.
bool isPeonyDesktopCmdline(const std::string& cmdline);

// The PEONY_ALPHA_WALLPAPER value: the paths the shim may be asked to
// match, colon-separated. Order is not semantic — the shim replaces the
// pixmap on a hit against any entry, and also matches anything under
// accountsservice's background store — so the list only has to be complete
// and free of duplicates.
//
//   1. |previous|  — what the user had before setup: if the accountsservice
//                    write fails, peony still loads this path
//   2. |normalized|— the /var/lib/AccountsService path accountsservice
//                    copied the marker to
//   3. |marker|    — the file setup just wrote
//
// Empty entries are dropped. An entry equal to one already present is
// dropped (exact path comparison, not the substring test this replaces — a
// path that merely contains another is a distinct candidate).
std::string buildWallpaperList(const std::string& marker, const std::string& normalized,
                               const std::string& previous);

// The environment the injected peony is launched with. The shim reads all
// three at load time; without PEONY_ALPHA_WALLPAPER it stays inert.
std::map<std::string, std::string> buildShimEnvironment(const std::string& shimPath,
                                                        const std::string& wallpaperList,
                                                        const std::string& logPath);

} // namespace Integration
