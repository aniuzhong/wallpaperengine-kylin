// wallpaper-engine: UI and CLI control surface for the Kylin
// linux-wallpaperengine integration. The UI is the editor of the
// wallpaper-engine systemd user unit; the wallpaper itself runs under systemd
// and survives a UI exit.
#include "cli.h"
#include "config.h"
#include "mainwindow.h"

#include <QApplication>
#include <cstdio>

int main (int argc, char** argv) {
    QApplication app (argc, argv);
    QApplication::setApplicationName ("wallpaper-engine");

    const QStringList args = QCoreApplication::arguments ();

    // headless control plane: `wallpaper-engine <command>` never opens a window
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

    MainWindow window;
    window.show ();
    return app.exec ();
}
