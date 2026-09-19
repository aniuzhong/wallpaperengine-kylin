#include "detailpanel.h"
#include "placeholder.h"

#include <QMovie>
#include <QPainter>
#include <QVBoxLayout>

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

void DetailPanel::stopMovie () {
    if (m_movie != nullptr) {
        m_movie->stop ();
        delete m_movie;
        m_movie = nullptr;
    }
    delete m_buffer;
    m_buffer = nullptr;
    m_animData.clear ();
}

void DetailPanel::showEntry (const WallpaperEntry& entry) {
    // the service hands over a display-resolution image; the panel only
    // renders it (placeholder when the wallpaper has no preview). Animated
    // previews play the bytes the service handed over.
    stopMovie ();
    m_title->setText (entry.title);
    m_meta->setText (entry.type + " · " + entry.size);

    if (!entry.previewAnim.isEmpty ()) {
        m_animData = entry.previewAnim;
        m_buffer = new QBuffer (&m_animData, this);
        m_buffer->open (QIODevice::ReadOnly);
        m_movie = new QMovie (m_buffer, QByteArray (), this);
        m_movie->setScaledSize (QSize (kPreviewW, kPreviewH));
        connect (m_movie, &QMovie::frameChanged, this,
                 [this] { m_preview->setPixmap (m_movie->currentPixmap ()); });
        m_movie->start ();
        return;
    }

    if (entry.preview.isNull ()) {
        QPixmap placeholder (kPreviewW, kPreviewH);
        QPainter painter (&placeholder);
        paintPlaceholder (&painter, placeholder.rect (), this->palette (), entry.type);
        m_preview->setPixmap (placeholder);
    } else {
        m_preview->setPixmap (QPixmap::fromImage (entry.preview));
    }
}

void DetailPanel::clear () {
    stopMovie ();
    m_preview->setPixmap (QPixmap ());
    m_title->setText (QString ());
    m_meta->setText (QString ());
}
