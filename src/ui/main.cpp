// wallpaper-engine: UI and CLI control surface for the Kylin linux-wallpaperengine
// integration. Early scaffold: only the config self-test is wired up; the
// wallpaper library grid, settings form and tray land with later commits.
#include "config.h"

#include <QCoreApplication>
#include <cstdio>

int main (int argc, char** argv) {
    QCoreApplication app (argc, argv);
    QCoreApplication::setApplicationName ("wallpaper-engine");

    const QStringList args = QCoreApplication::arguments ();

    if (args.contains ("--selftest")) {
	// load the config (creating defaults on first run) and report
	const auto config = Config::load ();
	std::printf ("config path: %s\n", Config::configPath ().toUtf8 ().constData ());
	std::printf ("engine: %s\n", config.enginePath.toUtf8 ().constData ());
	std::printf ("screens: %d, fps: %d, silent: %s\n", static_cast<int> (config.screens.size ()), config.fps,
		     config.silent ? "true" : "false");
	return config.save () ? 0 : 1;
    }

    std::printf ("wallpaper-engine: UI scaffold — nothing to show yet\n");
    std::printf ("usage: wallpaper-engine [--selftest]\n");
    return 0;
}
