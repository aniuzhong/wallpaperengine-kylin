#include "detailpanel.h"

#include <QPainter>
#include <QVBoxLayout>

namespace {
constexpr int kPreviewW = 360;
constexpr int kPreviewH = 202; // 16:9
}

DetailPanel::DetailPanel (QWidget* parent) : QWidget (parent) {
    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (16, 16, 16, 16);
    layout->setSpacing (10);

    m_preview = new QLabel (this);
    m_preview->setFixedSize (kPreviewW, kPreviewH);
    m_preview->setAlignment (Qt::AlignCenter);
    layout->addWidget (m_preview);

    m_title = new QLabel (this);
    m_title->setWordWrap (true);
    m_title->setStyleSheet ("font-size: 15px; font-weight: 600;");
    layout->addWidget (m_title);

    m_meta = new QLabel (this);
    layout->addWidget (m_meta);

    layout->addStretch (1);
    clear ();
}

void DetailPanel::showEntry (const QString& title, const QString& type, const QString& size,
                             const QImage& preview) {
    // the service hands over a display-resolution image; the panel only
    // renders it (placeholder when the wallpaper has no preview)

    if (preview.isNull ()) {
        const QPalette& palette = this->palette ();
        QLinearGradient gradient (0, 0, kPreviewW, kPreviewH);
        gradient.setColorAt (0, palette.color (QPalette::Mid));
        gradient.setColorAt (1, palette.color (QPalette::Window));
        QPixmap placeholder (kPreviewW, kPreviewH);
        QPainter painter (&placeholder);
        painter.fillRect (0, 0, kPreviewW, kPreviewH, gradient);
        painter.setPen (palette.color (QPalette::PlaceholderText));
        painter.drawText (placeholder.rect (), Qt::AlignCenter, type.toUpper ());
        m_preview->setPixmap (placeholder);
    } else {
        m_preview->setPixmap (QPixmap::fromImage (preview));
    }

    m_title->setText (title);
    m_meta->setText (type + " · " + size);
}

void DetailPanel::clear () {
    m_preview->setPixmap (QPixmap ());
    m_title->setText (QString ());
    m_meta->setText (QString ());
}
