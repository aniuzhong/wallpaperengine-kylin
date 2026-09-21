// T0 pure-logic tests: the integration marker's rendering half — BMP
// structure, palette, determinism. The xcb size probe that feeds it stays
// with the backend (integration/peony.cpp) and is not covered here.
#include "../src/integration/marker.h"

#include <QtTest>

#include <cstdint>
#include <cstring>
#include <string>

class MarkerTest : public QObject {
    Q_OBJECT

private slots:
    void bmpIsWellFormed() {
        const std::string bmp = Marker::renderBmp(320, 200);
        QVERIFY(bmp.size() > 54);
        QVERIFY(std::memcmp(bmp.data(), "BM", 2) == 0);

        uint32_t declared = 0;
        std::memcpy(&declared, bmp.data() + 2, 4);
        QCOMPARE(declared, static_cast<uint32_t>(bmp.size()));

        int32_t width = 0;
        int32_t height = 0;
        std::memcpy(&width, bmp.data() + 18, 4);
        std::memcpy(&height, bmp.data() + 22, 4);
        QCOMPARE(width, 320);
        QCOMPARE(height, 200);
    }

    void backgroundAndInkArePresent() {
        const std::string bmp = Marker::renderBmp(320, 200);
        // BMP rows are bottom-up: the first pixel row in file order is the
        // image's top row, and its first pixel must be the blue background
        const auto* pixels = reinterpret_cast<const unsigned char*>(bmp.data()) + 54;
        QCOMPARE(pixels[0], 0xd7);
        QCOMPARE(pixels[1], 0x78);
        QCOMPARE(pixels[2], 0x00);

        // and the text must have drawn some white ink somewhere
        size_t ink = 0;
        for (size_t i = 0; i + 2 < bmp.size() - 54; i += 3) {
            const auto* p = pixels + i;
            if (p[0] == 0xff && p[1] == 0xff && p[2] == 0xff)
                ink++;
        }
        QVERIFY(ink > 100);
    }

    void renderIsDeterministic() {
        QCOMPARE(Marker::renderBmp(320, 200), Marker::renderBmp(320, 200));
    }
};

QTEST_GUILESS_MAIN(MarkerTest)
#include "marker_test.moc"
