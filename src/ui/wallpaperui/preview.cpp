#include "preview.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>

QImage decodePreview(const QString& previewPath) {
    if (previewPath.isEmpty())
        return {};

    QImageReader reader(previewPath);
    const QSize target(kPreviewW, kPreviewH);
    reader.setScaledSize(target.scaled(target.width(), target.height(), Qt::KeepAspectRatioByExpanding));
    QImage preview = reader.read();
    if (!preview.isNull())
        preview = preview.copy((preview.width() - target.width()) / 2,
                               (preview.height() - target.height()) / 2,
                                target.width(), target.height());
    return preview;
}

bool isAnimatedPreview(const QString& previewPath) {
    return QFileInfo (previewPath).suffix ().compare ("gif", Qt::CaseInsensitive) == 0;
}
