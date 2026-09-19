#include "wallpaperui.h"

#include <QLabel>
#include <Qt>

WallpaperUI::WallpaperUI (QWidget* parent) : QMainWindow (parent) {
    setWindowTitle ("Wallpaper UI");
    resize (960, 640);

    // M0 scaffold: a read-only placeholder until the M1 library view lands
    auto* placeholder = new QLabel ("Wallpaper UI — scaffold (library view lands in M1)", this);
    placeholder->setAlignment (Qt::AlignCenter);
    setCentralWidget (placeholder);
}
