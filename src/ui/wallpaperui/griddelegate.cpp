#include "griddelegate.h"

#include <QIcon>
#include <QPainter>

namespace {
constexpr int kThumbW = 192;
constexpr int kThumbH = 108;   // 16:9
constexpr int kTileMargin = 6;
constexpr int kTitleBarH = 26;
}

GridDelegate::GridDelegate (QObject* parent) : QStyledItemDelegate (parent) {}

QSize GridDelegate::sizeHint (const QStyleOptionViewItem&, const QModelIndex&) const {
    return QSize (kThumbW + kTileMargin * 2, kThumbH + kTitleBarH + kTileMargin * 2);
}

void GridDelegate::paint (QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const {
    painter->save ();
    painter->setRenderHint (QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint (QPainter::Antialiasing, true);

    const QRect tile (option.rect.left () + kTileMargin, option.rect.top () + kTileMargin,
                      kThumbW, kThumbH);
    const QString title = index.data (Qt::DisplayRole).toString ();
    const QString type = index.data (Qt::UserRole).toString ();
    const QIcon icon = index.data (Qt::DecorationRole).value<QIcon> ();
    const QPalette& palette = option.palette;

    // thumbnail or palette-gradient placeholder with a type badge
    if (icon.isNull ()) {
        QLinearGradient gradient (tile.topLeft (), tile.bottomRight ());
        gradient.setColorAt (0, palette.color (QPalette::Mid));
        gradient.setColorAt (1, palette.color (QPalette::Window));
        painter->fillRect (tile, gradient);
        painter->setPen (palette.color (QPalette::PlaceholderText));
        painter->drawText (tile, Qt::AlignCenter, type.toUpper ());
    } else {
        icon.paint (painter, tile);
    }

    // translucent black title bar stays readable over any thumbnail in any
    // theme — this is readability, not theme
    const QRect titleBar (tile.left (), tile.bottom () - kTitleBarH + 1, kThumbW, kTitleBarH);
    painter->fillRect (titleBar, QColor (0, 0, 0, 140));
    painter->setPen (QColor (Qt::white));
    painter->drawText (titleBar.adjusted (8, 0, -8, 0),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       painter->fontMetrics ().elidedText (title, Qt::ElideRight, kThumbW - 16));

    // native highlight for the selected tile
    if (option.state & QStyle::State_Selected) {
        painter->setPen (QPen (palette.color (QPalette::Highlight), 2));
        painter->drawRect (tile.adjusted (-1, -1, 1, 1));
    }
    painter->restore ();
}
