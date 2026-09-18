#pragma once

#include "config.h"
#include "library.h"

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>

// Minimal control surface: thumbnail grid of the workshop library,
// double-click applies the selected wallpaper by restarting the lwe-engine
// systemd user unit. The UI is only the editor of the unit — closing it
// never affects a running wallpaper.
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow (QWidget* parent = nullptr);

private:
    void buildUi ();
    void refreshLibrary ();
    void selectCurrentWallpaper ();
    void applySelected ();
    void togglePause ();
    void updateStatus ();
    void checkIntegration ();
    void runIntegrationSetup ();

    Config m_config;
    QList<WallpaperEntry> m_library;
    QString m_screenName;

    QListWidget* m_libraryWidget = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_integrationBanner = nullptr;
    QPushButton* m_setupButton = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QTimer m_statusTimer;
};
