#pragma once

#include <QLabel>
#include <QWidget>

// Right-hand detail column. v1 shows static information only (preview,
// title, type and size); the apply buttons (M2) and the properties editor
// (M4) slot into this panel later.
class DetailPanel final : public QWidget {
public:
    explicit DetailPanel (QWidget* parent = nullptr);

    void showEntry (const QString& title, const QString& type, const QString& size,
                    const QImage& preview);
    void clear ();

private:
    QLabel* makePreviewLabel ();
    QLabel* m_preview = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_meta = nullptr;
};
