#pragma once

#include "../service/library.h"

#include <QBuffer>
#include <QByteArray>
#include <QLabel>
#include <QWidget>

class QMovie;

// Right-hand detail column: renders one service-layer WallpaperEntry —
// preview (animated when the entry ships a gif preview), title, type and
// size. The apply buttons (M2) and the properties editor (M4) slot into
// this panel later.
class DetailPanel final : public QWidget {
    Q_OBJECT
public:
    explicit DetailPanel (QWidget* parent = nullptr);

    void showEntry (const WallpaperEntry& entry);
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
