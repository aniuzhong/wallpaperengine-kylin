// The integration marker, rendered at setup time — a "blue screen" drawn
// programmatically: background, a sad face, and recovery instructions.
//
// Two libraries carry what the first version did in code: FreeType renders
// the text from the distribution's default English font, and stb_image_write
// encodes the PNG. The candidate list below IS the font discovery —
// fontconfig is deliberately not involved, because the marker is a recovery
// surface and must not grow a discovery stack. Without a usable font the
// screen degrades to the plain background: it still marks the failure
// state, only without instructions. The PNG is encoded into a memory buffer
// and written atomically like every other file of the product; stb's
// direct-to-file entry points would bypass that.

#include "marker.h"

#include "../posix.h"

// the header's `= { 0 }` idiom trips -Wmissing-field-initializers under
// -Wextra; the noise is stb's, not ours
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <string>
#include <vector>

namespace marker {
namespace {

// Win10 blue and the text color, as RGB — stb writes PNG from RGB order.
constexpr unsigned char kBg[3] = {0x00, 0x78, 0xd7};
constexpr unsigned char kFg[3] = {0xff, 0xff, 0xff};

// Distribution-default English monospace fonts, in order; the first
// readable file wins. Paths observed on Debian/Ubuntu-family layouts,
// which includes the Kylin base.
constexpr const char* kFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

bool findFont(FT_Library library, FT_Face* face) {
    for (const char* path : kFontCandidates) {
        if (FT_New_Face(library, path, 0, face) == 0)
            return true;
    }
    return false;
}

// Draw |text| with its baseline at |baseline|, starting at pen position
// |x|; the pen advances past the drawn text. Glyphs render at the pixel
// size |size|.
void drawText(std::vector<unsigned char>& px, int width, int height, int& x, int baseline, const std::string& text,
              FT_Face face, int size) {
    FT_Set_Pixel_Sizes(face, 0, size);
    for (const char c : text) {
        if (FT_Load_Char(face, static_cast<unsigned char>(c), FT_LOAD_RENDER) != 0)
            continue;
        const FT_GlyphSlot glyph = face->glyph;
        const FT_Bitmap& bitmap = glyph->bitmap;
        const int left = x + glyph->bitmap_left;
        const int top = baseline - glyph->bitmap_top;
        for (int row = 0; row < static_cast<int>(bitmap.rows); row++) {
            const int py = top + row;
            if (py < 0 || py >= height)
                continue;
            for (int col = 0; col < static_cast<int>(bitmap.width); col++) {
                const int pxx = left + col;
                if (pxx < 0 || pxx >= width)
                    continue;
                // the antialiased coverage blends the ink over whatever is
                // beneath it, background or earlier glyphs
                const unsigned char alpha = bitmap.buffer[static_cast<size_t>(row) * bitmap.pitch + col];
                unsigned char* pixel = &px[(static_cast<size_t>(py) * width + pxx) * 3];
                for (int i = 0; i < 3; i++)
                    pixel[i] = static_cast<unsigned char>((kFg[i] * alpha + pixel[i] * (255 - alpha)) / 255);
            }
        }
        x += glyph->advance.x >> 6;
    }
}

// stb writes through a callback, so the atomic write stays in one place
std::string encode(int width, int height, const std::vector<unsigned char>& px) {
    std::string png;
    png.reserve(64 * 1024);
    const int ok = stbi_write_png_to_func(
        [](void* context, void* data, int size) {
            static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<size_t>(size));
        },
        &png, width, height, 3, px.data(), width * 3);
    if (!ok)
        return {};
    return png;
}

// baseline offset from the line's top at |size|, from the face metrics
int ascent(FT_Face face, int size) {
    FT_Set_Pixel_Sizes(face, 0, size);
    return face->size->metrics.ascender >> 6;
}

} // namespace

bool FontFound() {
    FT_Library library;
    if (FT_Init_FreeType(&library) != 0)
        return false;
    FT_Face face = nullptr;
    const bool found = findFont(library, &face);
    if (found)
        FT_Done_Face(face);
    FT_Done_FreeType(library);
    return found;
}

std::string RenderPng(int width, int height) {
    // integer scale from the resolution: 4K -> 6x, 1080p -> 3x, floor 1x
    const int scale = std::max(1, std::min(width / 640, height / 360));
    const int margin = 20 * scale;
    const int textSize = 13 * scale;
    const int faceSize = textSize * 4; // the sad face, 4x like the old bitmap

    std::vector<unsigned char> px(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0; i < px.size(); i += 3) {
        px[i] = kBg[0];
        px[i + 1] = kBg[1];
        px[i + 2] = kBg[2];
    }

    FT_Library library;
    if (FT_Init_FreeType(&library) != 0)
        return encode(width, height, px);
    FT_Face face = nullptr;
    if (!findFont(library, &face)) {
        FT_Done_FreeType(library);
        return encode(width, height, px);
    }

    // line rhythm kept from the bitmap-font layout: 18*scale per text line,
    // wider gaps around the face and the stop code
    int x = margin;
    int top = margin;
    drawText(px, width, height, x, top + ascent(face, faceSize), ":(", face, faceSize);
    top += 16 * scale * 4 + 6 * scale;

    for (const char* line : {"The dynamic wallpaper integration is not active.",
                             "Your desktop is showing this image because the wallpaper",
                             "service could not attach to the desktop shell."}) {
        x = margin;
        drawText(px, width, height, x, top + ascent(face, textSize), line, face, textSize);
        top += 18 * scale;
    }
    top += 6 * scale;

    x = margin;
    drawText(px, width, height, x, top + ascent(face, textSize), "Stop code: INTEGRATION_NOT_ACTIVE", face, textSize);
    top += 22 * scale;

    x = margin;
    drawText(px, width, height, x, top + ascent(face, textSize), "To restore:", face, textSize);
    top += 18 * scale;
    x = margin + 4 * scale;
    drawText(px, width, height, x, top + ascent(face, textSize), "$ wallpaper-engine setup-integration", face, textSize);
    top += 22 * scale;

    x = margin;
    drawText(px, width, height, x, top + ascent(face, textSize), "To diagnose:", face, textSize);
    top += 18 * scale;
    x = margin + 4 * scale;
    drawText(px, width, height, x, top + ascent(face, textSize), "$ wallpaper-engine doctor", face, textSize);

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return encode(width, height, px);
}

bool WriteTo(const std::string& path, int width, int height) {
    const std::string png = RenderPng(width, height);
    if (png.empty())
        return false;
    return we::WriteFileAtomic(path, png).has_value();
}

} // namespace marker
