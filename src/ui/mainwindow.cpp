#include "mainwindow.h"

#include "integration.h"
#include "engineunit.h"

#include <QGuiApplication>
#include <QScreen>
#include <QImageReader>
#include <QVBoxLayout>
#include <QApplication>

MainWindow::MainWindow (QWidget* parent) : QMainWindow (parent) {
    m_config = Config::load ();

    buildUi ();
    refreshLibrary ();
    selectCurrentWallpaper ();
    updateStatus ();

    m_statusTimer.setInterval (3000);
    connect (&m_statusTimer, &QTimer::timeout, this, [this] {
        updateStatus ();
        checkIntegration ();
    });
    m_statusTimer.start ();
    checkIntegration ();
}

void MainWindow::buildUi () {
    setWindowTitle ("wallpaper-engine");
    resize (840, 560);

    auto* central = new QWidget (this);
    auto* layout = new QVBoxLayout (central);

    m_libraryWidget = new QListWidget (central);
    m_libraryWidget->setViewMode (QListView::IconMode);
    m_libraryWidget->setIconSize (QSize (220, 124));
    m_libraryWidget->setGridSize (QSize (236, 156));
    m_libraryWidget->setMovement (QListView::Static);
    m_libraryWidget->setResizeMode (QListView::Adjust);
    m_libraryWidget->setWordWrap (true);
    m_libraryWidget->setUniformItemSizes (true);
    layout->addWidget (m_libraryWidget, 1);

    // integration banner: shown while peony runs without the shim, with a
    // one-click setup that injects and verifies the whole chain
    m_integrationBanner = new QLabel ("Desktop integration is not configured — the wallpaper will not be visible.",
                                      central);
    m_integrationBanner->setStyleSheet ("background: #fff3cd; color: #664d03; padding: 8px; border-radius: 4px;");
    m_integrationBanner->setWordWrap (true);
    layout->addWidget (m_integrationBanner);

    auto* integrationRow = new QHBoxLayout;
    m_setupButton = new QPushButton ("Set up integration", central);
    m_setupButton->hide ();
    connect (m_setupButton, &QPushButton::clicked, this, [this] { runIntegrationSetup (); });
    integrationRow->addStretch (1);
    integrationRow->addWidget (m_setupButton);
    layout->addLayout (integrationRow);

    auto* controlsRow = new QHBoxLayout;
    m_statusLabel = new QLabel (central);
    controlsRow->addWidget (m_statusLabel, 1);

    auto* pauseButton = new QPushButton ("Pause", central);
    connect (pauseButton, &QPushButton::clicked, this, [this] { togglePause (); });
    controlsRow->addWidget (pauseButton);
    m_pauseButton = pauseButton;

    auto* applyButton = new QPushButton ("Apply selected", central);
    connect (applyButton, &QPushButton::clicked, this, [this] { applySelected (); });
    controlsRow->addWidget (applyButton);
    layout->addLayout (controlsRow);

    setCentralWidget (central);

    // current screen name (X11 output name on the xcb platform, e.g. DP-0)
    if (const QScreen* screen = QGuiApplication::primaryScreen ())
        m_screenName = screen->name ();
    if (m_screenName.isEmpty ())
        m_screenName = "DP-0";

    connect (m_libraryWidget, &QListWidget::itemDoubleClicked, this, [this] (QListWidgetItem*) { applySelected (); });
}

void MainWindow::refreshLibrary () {
    m_libraryWidget->clear ();
    m_library = scanLibrary (m_config.workshopDir);

    for (const WallpaperEntry& entry : m_library) {
        QIcon icon;
        if (!entry.previewPath.isEmpty ()) {
            QImageReader reader (entry.previewPath);
            reader.setScaledSize (m_libraryWidget->iconSize ());
            icon = QIcon (QPixmap::fromImage (reader.read ()));
        }
        auto* item = new QListWidgetItem (icon, entry.title);
        item->setData (Qt::UserRole, entry.id);
        item->setToolTip (QString ("%1\n%2 [%3]").arg (entry.title, entry.id, entry.type));
        m_libraryWidget->addItem (item);
    }
}

void MainWindow::selectCurrentWallpaper () {
    const QString current = m_config.screens.value (m_screenName);
    if (current.isEmpty ())
        return;
    for (int i = 0; i < m_libraryWidget->count (); i++) {
        if (m_libraryWidget->item (i)->data (Qt::UserRole).toString () == current) {
            m_libraryWidget->setCurrentRow (i);
            return;
        }
    }
}

void MainWindow::applySelected () {
    auto* item = m_libraryWidget->currentItem ();
    if (item == nullptr)
        return;

    const QString id = item->data (Qt::UserRole).toString ();
    m_config.screens[m_screenName] = id;

    // persist config, regenerate the unit from it, then restart the engine.
    // the UI is only the editor of the unit; the wallpaper survives a UI exit.
    m_config.save ();
    const bool ok = EngineUnit::writeUnitFile (m_config) && EngineUnit::daemonReload () && EngineUnit::restartUnit ();

    if (ok) {
        m_statusLabel->setText (QString ("applied: %1 [%2] on %3").arg (item->text (), id, m_screenName));
    } else {
        m_statusLabel->setText ("failed to apply — is the systemd user session available?");
    }
    updateStatus ();
}

void MainWindow::checkIntegration () {
    const Integration::Status status = Integration::detect ();
    const bool configured = status.configured ();
    m_integrationBanner->setVisible (!configured);
    m_setupButton->setVisible (!configured);
}

void MainWindow::runIntegrationSetup () {
    m_setupButton->setEnabled (false);
    m_setupButton->setText ("Setting up...");
    QApplication::setOverrideCursor (Qt::WaitCursor);

    QString error;
    const bool ok = Integration::setup (&error);

    QApplication::restoreOverrideCursor ();
    m_setupButton->setEnabled (true);
    m_setupButton->setText ("Set up integration");
    checkIntegration ();
    if (ok)
        m_statusLabel->setText ("integration configured — double-click a thumbnail to apply a wallpaper");
    else
        m_statusLabel->setText ("integration failed: " + error);
}

void MainWindow::togglePause () {
    if (EngineUnit::unitState () == "active") {
        EngineUnit::stopUnit ();
    } else {
        EngineUnit::restartUnit ();
    }
    updateStatus ();
}

void MainWindow::updateStatus () {
    const QString state = EngineUnit::unitState ();
    const QString current = m_config.screens.value (m_screenName);
    m_statusLabel->setText (QString ("engine: %1 · screen %2: %3").arg (state, m_screenName, current));
    m_pauseButton->setText (state == "active" ? "Pause" : "Resume");
}
