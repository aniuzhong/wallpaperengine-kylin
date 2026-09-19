#pragma once

#include <QMainWindow>

// Wallpaper UI — the new master-detail frontend. M0 scaffold: an empty
// shell behind `--wallpaper-ui`; it becomes the default entry at M2 once
// feature parity with the legacy UI is signed off (plan: tmp/plan.md).
class WallpaperUI final : public QMainWindow {
public:
    explicit WallpaperUI (QWidget* parent = nullptr);
};
