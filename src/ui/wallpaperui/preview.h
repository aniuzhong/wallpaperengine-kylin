#pragma once

#include <QImage>
#include <QString>

// Presentation side of the library entries: the service hands out preview
// paths only; the ui decides how they render and at what resolution.

// Display size of the decoded preview image: 16:9, one decode serves the
// grid tile (downscaled by the delegate) and the detail panel (native size).
constexpr int kPreviewW = 360;
constexpr int kPreviewH = kPreviewW * 9 / 16;

// Decode a preview file at display size, center-cropped to exactly 16:9 so
// grid tile and detail panel distort nothing regardless of the source aspect
// ratio. Returns a null image when the path is empty or cannot be decoded.
QImage decodePreview(const QString& previewPath);

// Multi-frame previews (gif) play in the detail panel; every other format
// renders as a static image.
bool isAnimatedPreview(const QString& previewPath);
