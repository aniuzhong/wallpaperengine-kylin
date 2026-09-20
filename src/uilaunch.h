#pragma once

#include <string>
#include <vector>

// Opening the UI in the user's browser. The default browser is discovered
// the way the desktop itself does it — xdg-settings names a .desktop file,
// whose Exec= line is the only thing that knows how to start it. That
// matters here: on Kylin the default browser may be a Flatpak, a Snap, or a
// tarball in someone's home directory, and each has a different invocation.
namespace Ui {

struct Launch {
    std::vector<std::string> argv; // the command line to run
    bool appWindow = false;        // --app= was used: no tab strip, no address bar
};

// Pure: the Exec= line of the [Desktop Entry] group. A desktop file can hold
// several Exec lines — the rest belong to desktop actions (Kylin's
// qaxbrowser entry ships "New Window" and "New Incognito Window" actions
// whose Execs differ), so only the main group's line may be used.
std::string parseDesktopExec(const std::string& desktopFileContents);

// Pure: one Exec line into argv, per the desktop-entry quoting rules (double
// quotes, with backslash escapes for " \ ` $). Empty quoted arguments are
// preserved; unquoted empty ones cannot exist.
std::vector<std::string> parseExecLine(const std::string& execLine);

// Pure: whether an Exec line names a Chromium-family browser — the only kind
// where --app= means anything. Matching on the name is a heuristic, but the
// failure mode is benign: a browser that does not know the switch ignores it
// and opens an ordinary window.
bool isChromiumFamily(const std::string& execLine);

// Pure: the command line that opens |url|. Field codes (%u %U %f %F %i %c
// %k) are removed. A Chromium-family browser is asked for an app window.
Launch launchFor(const std::string& execLine, const std::string& url);

// Impure: the Exec= line of the default browser, resolved along the XDG data
// directories. Empty when no default is configured or its desktop file
// cannot be read.
std::string defaultBrowserExec();

// Impure: open |url|. False with |error| filled when nothing could be
// started, so the caller can print the URL instead of failing silently.
bool openBrowser(const std::string& url, std::string* error);

} // namespace Ui
