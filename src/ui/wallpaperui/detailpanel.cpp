#include "detailpanel.h"
#include "placeholder.h"
#include "preview.h"

#include <QFile>
#include <QMovie>
#include <QPainter>
#include <QVBoxLayout>

namespace {
// presentation formatting of the service-provided byte count; the only
// consumer of the size in this frontend
QString humanizeSize(uint64_t bytes) {
    if (bytes >= (1ULL << 30))
        return QString::number(bytes / double (1ULL << 30), 'f', 1) + " GB";
    if (bytes >= (1ULL << 20))
        return QString::number(bytes / double (1ULL << 20), 'f', 1) + " MB";
    if (bytes >= (1ULL << 10))
        return QString::number(bytes / double (1ULL << 10), 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}
} // namespace

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
    // the service hands over the preview path; this panel decodes it for its
    // own display size (placeholder when the wallpaper ships no preview).
    // Animated previews (gif) play in place.
    stopMovie ();
    m_title->setText (QString::fromStdString (entry.title));
    m_meta->setText (QString::fromStdString (entry.type) + " · " + humanizeSize (entry.sizeBytes));

    const QString previewPath = QString::fromStdString (entry.previewPath);
    if (isAnimatedPreview (previewPath)) {
        QFile anim (previewPath);
        if (anim.open (QIODevice::ReadOnly)) {
            m_animData = anim.readAll ();
            m_buffer = new QBuffer (&m_animData, this);
            m_buffer->open (QIODevice::ReadOnly);
            m_movie = new QMovie (m_buffer, QByteArray (), this);
            m_movie->setScaledSize (QSize (kPreviewW, kPreviewH));
            connect (m_movie, &QMovie::frameChanged, this,
                     [this] { m_preview->setPixmap (m_movie->currentPixmap ()); });
            m_movie->start ();
            return;
        }
    }

    const QImage preview = decodePreview (previewPath);
    if (preview.isNull ()) {
        QPixmap placeholder (kPreviewW, kPreviewH);
        QPainter painter (&placeholder);
        paintPlaceholder (&painter, placeholder.rect (), this->palette (), QString::fromStdString (entry.type));
        m_preview->setPixmap (placeholder);
    } else {
        m_preview->setPixmap (QPixmap::fromImage (preview));
    }
}

void DetailPanel::clear () {
    stopMovie ();
    m_preview->setPixmap (QPixmap ());
    m_title->setText (QString ());
    m_meta->setText (QString ());
}
