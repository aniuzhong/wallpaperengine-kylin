#pragma once

#include <QSize>
#include <QStyledItemDelegate>

// Paints one library tile the way the original does: thumbnail with the
// title on a translucent bar at its bottom, accent border when selected,
// dark gradient placeholder when no preview exists. Colors come from
// Theme; item roles: DisplayRole = title, DecorationRole = thumbnail,
// UserRole = type (placeholder badge text).
class GridDelegate final : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GridDelegate (QObject* parent = nullptr);

    // the full tile geometry, margins included — the grid's gridSize must
    // come from here, never from a copy of these numbers
    static QSize tileSize ();

    void paint (QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index) const override;
    QSize sizeHint (const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
