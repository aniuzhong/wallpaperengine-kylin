#include "placeholder.h"

#include <QLinearGradient>
#include <QPainter>

void paintPlaceholder (QPainter* painter, const QRect& rect, const QPalette& palette, const QString& type) {
    QLinearGradient gradient (rect.topLeft (), rect.bottomRight ());
    gradient.setColorAt (0, palette.color (QPalette::Mid));
    gradient.setColorAt (1, palette.color (QPalette::Window));
    painter->fillRect (rect, gradient);
    painter->setPen (palette.color (QPalette::PlaceholderText));
    painter->drawText (rect, Qt::AlignCenter, type.toUpper ());
}
