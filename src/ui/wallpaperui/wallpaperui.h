#pragma once

#include "../service/library.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>

// Wallpaper UI shell (M1): a read-only library browser in the style of the
// original. All data comes from the service layer (library scan + config) —
// the ui knows no paths, performs no file access and holds no directory
// knowledge; it only consumes the WallpaperEntry images and strings.
class WallpaperUI final : public QMainWindow {
public:
    explicit WallpaperUI (QWidget* parent = nullptr);

private:
    void buildToolbar ();
    void buildBody ();
    const WallpaperEntry* findEntry (const QString& id) const;
    QList<WallpaperEntry> filtered () const;
    void rebuildGrid ();
    void showDetail (QListWidgetItem* current);
    void applyEntry (const WallpaperEntry& entry);

    QList<WallpaperEntry> m_entries;

    QLineEdit* m_search = nullptr;
    QComboBox* m_type = nullptr;
    QComboBox* m_sort = nullptr;
    QListWidget* m_grid = nullptr;
    class DetailPanel* m_detail = nullptr;
};
