#include "wallpaperui.h"
#include "detailpanel.h"
#include "griddelegate.h"

#include "../service/config.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>

WallpaperUI::WallpaperUI (QWidget* parent) : QMainWindow (parent) {
    setWindowTitle ("Wallpaper UI 0.1.0 - Kylin Edition");
    resize (1180, 720);

    m_entries = scanLibrary (Config::load ().workshopDir);

    buildToolbar ();
    buildBody ();
    rebuildGrid ();
}

void WallpaperUI::buildToolbar () {
    auto* toolbar = new QWidget (this);
    toolbar->setObjectName ("toolbar");
    auto* layout = new QHBoxLayout (toolbar);
    layout->setContentsMargins (10, 8, 10, 8);
    layout->setSpacing (10);

    m_search = new QLineEdit (toolbar);
    m_search->setPlaceholderText ("Search");
    m_search->setClearButtonEnabled (true);
    m_search->setFixedWidth (260);
    layout->addWidget (m_search);

    m_type = new QComboBox (toolbar);
    m_type->addItems ({ "All types", "scene", "video", "web" });
    layout->addWidget (m_type);

    m_sort = new QComboBox (toolbar);
    m_sort->addItems ({ "Name (A-Z)", "Name (Z-A)" });
    layout->addWidget (m_sort);

    layout->addStretch (1);

    // reserved: the settings dialog lands in M3 — inert in the shell
    layout->addWidget (new QPushButton ("Settings", toolbar));

    setMenuWidget (toolbar);

    connect (m_search, &QLineEdit::textChanged, this, [this] { rebuildGrid (); });
    connect (m_type, QOverload<int>::of (&QComboBox::currentIndexChanged), this, [this] { rebuildGrid (); });
    connect (m_sort, QOverload<int>::of (&QComboBox::currentIndexChanged), this, [this] { rebuildGrid (); });
}

void WallpaperUI::buildBody () {
    auto* splitter = new QSplitter (this);

    m_grid = new QListWidget (splitter);
    m_grid->setViewMode (QListWidget::IconMode);
    m_grid->setResizeMode (QListWidget::Adjust);
    m_grid->setMovement (QListWidget::Static);
    m_grid->setUniformItemSizes (true);
    m_grid->setGridSize (QSize (204, 146));
    m_grid->setItemDelegate (new GridDelegate (m_grid));
    m_grid->setVerticalScrollMode (QListWidget::ScrollPerPixel);
    splitter->addWidget (m_grid);

    m_detail = new DetailPanel (splitter);
    splitter->addWidget (m_detail);
    splitter->setStretchFactor (0, 3);
    splitter->setStretchFactor (1, 1);

    setCentralWidget (splitter);
    connect (m_grid, &QListWidget::itemSelectionChanged, this, [this] {
        showDetail (m_grid->currentItem ());
    });
}

QList<WallpaperEntry> WallpaperUI::filtered () const {
    const QString needle = m_search->text ().trimmed ();
    const QString type = m_type->currentIndex () == 0 ? QString () : m_type->currentText ();
    QList<WallpaperEntry> result;
    for (const WallpaperEntry& entry : m_entries) {
        if (!type.isEmpty () && entry.type != type)
            continue;
        if (!needle.isEmpty () && !entry.title.contains (needle, Qt::CaseInsensitive))
            continue;
        result.append (entry);
    }
    if (m_sort->currentIndex () == 1)
        std::sort (result.begin (), result.end (),
                   [] (const WallpaperEntry& a, const WallpaperEntry& b) { return a.title > b.title; });
    else
        std::sort (result.begin (), result.end (),
                   [] (const WallpaperEntry& a, const WallpaperEntry& b) {
                       return a.title.compare (b.title, Qt::CaseInsensitive) < 0;
                   });
    return result;
}

void WallpaperUI::rebuildGrid () {
    m_grid->clear ();
    for (const WallpaperEntry& entry : filtered ()) {
        auto* item = new QListWidgetItem (m_grid);
        item->setData (Qt::DisplayRole, entry.title);
        item->setData (Qt::UserRole, entry.type);
        if (!entry.preview.isNull ())
            item->setIcon (QIcon (QPixmap::fromImage (entry.preview)));
    }
    if (m_grid->count () > 0)
        m_grid->setCurrentRow (0);
    else
        m_detail->clear ();
}

void WallpaperUI::showDetail (QListWidgetItem* current) {
    if (current == nullptr)
        return;
    const QString title = current->data (Qt::DisplayRole).toString ();
    for (const WallpaperEntry& entry : m_entries) {
        if (entry.title == title) {
            m_detail->showEntry (entry.title, entry.type, entry.size, entry.preview, entry.previewAnim);
            return;
        }
    }
}
