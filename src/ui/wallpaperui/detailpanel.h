#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QLabel>
#include <QWidget>

class QMovie;

// Right-hand detail column. Static information (preview, title, type and
// size); multi-frame previews play here as an animation. The apply buttons
// (M2) and the properties editor (M4) slot into this panel later.
class DetailPanel final : public QWidget {
public:
    explicit DetailPanel (QWidget* parent = nullptr);

    void showEntry (const QString& title, const QString& type, const QString& size,
                    const QImage& preview, const QByteArray& previewAnim);
    void clear ();

private:
    void stopMovie ();

    QLabel* m_preview = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_meta = nullptr;
    QMovie* m_movie = nullptr;
    QBuffer* m_buffer = nullptr;
    QByteArray m_animData; // the movie's device references these bytes
};
