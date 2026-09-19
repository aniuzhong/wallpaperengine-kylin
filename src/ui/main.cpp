// wallpaper-engine: UI and CLI control surface for the Kylin
// linux-wallpaperengine integration. The UI is the editor of the
// wallpaper-engine systemd user unit; the wallpaper itself runs under systemd
// and survives a UI exit.
#include "cli.h"
#include "config.h"
#include "wallpaperui/wallpaperui.h"

#include <QApplication>
#include <QCoreApplication>
#include <cstdio>
#include <memory>

int main (int argc, char** argv) {
    // argv must be inspected before any app instance exists: arguments()
    // needs one, and WHICH instance is created decides whether the control
    // plane can run without a display
    QStringList args;
    for (int i = 0; i < argc; i++)
        args << QString::fromLocal8Bit (argv[i]);

    const bool headless =
        (args.size () > 1 && !args.at (1).startsWith ("-")) // control-plane command
        || args.contains ("--selftest");

    // headless control plane: `wallpaper-engine <command>` never opens a
    // window and must work with no display at all (SSH, CI, pre-login), so
    // it gets a plain QCoreApplication — no platform plugin is even loaded.
    // Only the GUI path needs QApplication.
    const std::unique_ptr<QCoreApplication> app =
        headless ? std::unique_ptr<QCoreApplication> (new QCoreApplication (argc, argv))
                 : std::unique_ptr<QCoreApplication> (new QApplication (argc, argv));
    QApplication::setApplicationName ("wallpaper-engine");

    if (args.size () > 1 && !args.at (1).startsWith ("-"))
        return runCli (args.mid (1));

    if (args.contains ("--selftest")) {
        // load the config (creating defaults on first run) and report
        const auto config = Config::load ();
        std::printf ("config path: %s\n", Config::configPath ().toUtf8 ().constData ());
        std::printf ("engine: %s\n", config.enginePath.toUtf8 ().constData ());
        std::printf ("screens: %d, fps: %d, silent: %s\n", static_cast<int> (config.screens.size ()), config.fps,
                     config.silent ? "true" : "false");
        return config.save () ? 0 : 1;
    }

    // Wallpaper UI is the only GUI: `wallpaper-engine` without arguments
    // opens it; the headless CLI remains the automation surface
    WallpaperUI window;
    window.show ();
    return app->exec ();
}
