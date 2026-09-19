#pragma once

#include <QPalette>
#include <QRect>
#include <QString>

class QPainter;

// Shared visual atom of the wallpaper ui: the "no preview" look — diagonal
// palette gradient with the entry type as a centered badge. The grid
// delegate and the detail panel both render through this so the placeholder
// is one identical visual, not two parallel implementations.
void paintPlaceholder (QPainter* painter, const QRect& rect, const QPalette& palette, const QString& type);
