// T0 pure-logic tests: the integration marker's rendering half — PNG
// structure, background/ink contrast, determinism. The xcb size probe that
// feeds it stays with the backend (integration/peony.cpp) and is not
// covered here.
#include "../src/integration/marker.h"

#include <QColor>
#include <QImage>
#include <QtTest>

#include <cstring>
#include <string>

class MarkerTest : public QObject {
    Q_OBJECT

private slots:
    void pngIsWellFormed() {
        const std::string png = marker::RenderPng(320, 200);
        QVERIFY(png.size() > 8);
        QVERIFY(std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
    }

    void backgroundAndInkArePresent() {
        const std::string png = marker::RenderPng(320, 200);
        QImage image;
        QVERIFY(image.loadFromData(reinterpret_cast<const unsigned char*>(png.data()),
                                   static_cast<int>(png.size())));
        QCOMPARE(image.width(), 320);
        QCOMPARE(image.height(), 200);

        // the top-left pixel is the blue background, untouched by any text
        QCOMPARE(image.pixelColor(0, 0), QColor(0x00, 0x78, 0xd7));

        if (!marker::FontFound())
            QSKIP("no candidate font on this machine — text coverage is environment-dependent");

        // and the text must have drawn non-background ink somewhere
        const QColor background(0x00, 0x78, 0xd7);
        int ink = 0;
        for (int y = 0; y < image.height(); y++)
            for (int x = 0; x < image.width(); x++)
                if (image.pixelColor(x, y) != background)
                    ink++;
        QVERIFY(ink > 100);
    }

    void renderIsDeterministic() {
        QCOMPARE(marker::RenderPng(320, 200), marker::RenderPng(320, 200));
    }
};

QTEST_GUILESS_MAIN(MarkerTest)
#include "marker_test.moc"
