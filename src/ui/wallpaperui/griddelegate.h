#pragma once

#include <QStyledItemDelegate>

// Paints one library tile the way the original does: thumbnail with the
// title on a translucent bar at its bottom, accent border when selected,
// dark gradient placeholder when no preview exists. Colors come from
// Theme; item roles: DisplayRole = title, DecorationRole = thumbnail,
// UserRole = type (placeholder badge text).
class GridDelegate final : public QStyledItemDelegate {
public:
    explicit GridDelegate (QObject* parent = nullptr);

    void paint (QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index) const override;
    QSize sizeHint (const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
