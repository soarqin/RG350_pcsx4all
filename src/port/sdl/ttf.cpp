#include "ttf.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <SDL.h>

#include <climits>
#include <fstream>

namespace TTF {

const int RECTPACK_WIDTH = 1024;

struct RectPackData {
    stbrp_context context;
    stbrp_node nodes[RECTPACK_WIDTH];
    uint8_t pixels[RECTPACK_WIDTH * RECTPACK_WIDTH];
};

Font::Font(const std::string& filename, int size, uint8_t mono_width, SDL_Surface *surface, int index) : ttfFilename_(filename), fontSize_(size), monoWidth_(mono_width) {
    for (uint16_t i = 0; i < 256; ++i) {
        uint8_t c;
#ifdef TTF_SHARP
        if (i < 128) c = i * 3 / 2;
        else c = i / 2 + 128;
#else
        c = i;
#endif
        depthColor_[i] = SDL_MapRGB(surface->format, c, c, c);
    }
    auto *info = new stbtt_fontinfo;
    std::ifstream fin(filename, std::ios::binary);
    fin.seekg(0, std::ios::end);
    size_t sz = fin.tellg();
    ttfBuffer_ = new uint8_t[sz];
    fin.seekg(0, std::ios::beg);
    fin.read(reinterpret_cast<char*>(ttfBuffer_), sz);
    fin.close();
    stbtt_InitFont(info, ttfBuffer_, stbtt_GetFontOffsetForIndex(ttfBuffer_, index));
    fontScale_ = stbtt_ScaleForMappingEmToPixels(info, static_cast<float>(size));
    font_ = info;

    newRectPack();
}

Font::~Font() {
    for (auto *&p: rpData_) delete p;
    rpData_.clear();

    delete[] ttfBuffer_;
    delete static_cast<stbtt_fontinfo*>(font_);

    ttfBuffer_ = nullptr;
    font_ = nullptr;
}

/* UTF-8 to UCS-4 */
static inline uint32_t utf8toucs4(const char *&text) {
    uint8_t c = static_cast<uint8_t>(*text);
    if (c < 0x80) {
        uint16_t ch = c;
        ++text;
        return ch;
    } else if (c < 0xC0) {
        return 0;
    } else if (c < 0xE0) {
        uint16_t ch = (c & 0x1Fu) << 6u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= c & 0x3Fu;
        ++text;
        return ch;
    } else if (c < 0xF0) {
        uint16_t ch = (c & 0x0Fu) << 12u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 6u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= c & 0x3Fu;
        ++text;
        return ch;
    } else if (c < 0xF8) {
        uint16_t ch = (c & 0x07u) << 18u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 12u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 6u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= c & 0x3Fu;
        ++text;
        return ch;
    } else if (c < 0xFC) {
        uint16_t ch = (c & 0x03u) << 24u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 18u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 12u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 6u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= c & 0x3Fu;
        ++text;
        return ch;
    } else if (c < 0xFE) {
        uint16_t ch = (c & 0x03u) << 30u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 24u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 18u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 12u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= (c & 0x3Fu) << 6u;
        c = static_cast<uint8_t>(*++text);
        if (c == 0) return 0;
        ch |= c & 0x3Fu;
        ++text;
        return ch;
    }
    return 0;
}

void Font::render(SDL_Surface *surface, int x, int y, const char *text) {
    auto *info = static_cast<stbtt_fontinfo*>(font_);

    uint16_t *output;
    int stride = surface->pitch / surface->format->BytesPerPixel;
    output = static_cast<uint16_t*>(surface->pixels) + stride * y + x;

    while (*text != 0) {
        uint32_t ch = utf8toucs4(text);
        if (ch == 0) break;

        /* Check if bitmap is already cached */
        int index = stbtt_FindGlyphIndex(info, ch);
        FontData *fd;
        auto ite = fontCache_.find(index);

        if (ite == fontCache_.end()) {
            fd = &fontCache_[index];

            /* Read font data to cache */
            int advW, leftB;
            stbtt_GetGlyphHMetrics(info, index, &advW, &leftB);
            fd->advW = static_cast<uint8_t>(fontScale_ * advW);
            fd->leftB = static_cast<uint8_t>(fontScale_ * leftB);
            int ix0, iy0, ix1, iy1;
            stbtt_GetGlyphBitmapBoxSubpixel(info, index, fontScale_, fontScale_, 3, 3, &ix0, &iy0, &ix1, &iy1);
            fd->ix0 = ix0;
            fd->iy0 = iy0;
            fd->w = ix1 - ix0;
            fd->h = iy1 - iy0;

            /* Get last rect pack bitmap */
            auto rpidx = rpData_.size() - 1;
            auto *rpd = rpData_[rpidx];
            stbrp_rect rc = {0, fd->w, fd->h};
            if (!stbrp_pack_rects(&rpd->context, &rc, 1)) {
                /* No space to hold the bitmap,
                 * create a new bitmap */
                newRectPack();
                rpidx = rpData_.size() - 1;
                rpd = rpData_[rpidx];
                stbrp_pack_rects(&rpd->context, &rc, 1);
            }
            /* Do rect pack */
            fd->rpx = rc.x;
            fd->rpy = rc.y;
            fd->rpidx = rpidx;
            stbtt_MakeGlyphBitmapSubpixel(info, &rpd->pixels[rc.y * RECTPACK_WIDTH + rc.x], fd->w, fd->h, RECTPACK_WIDTH, fontScale_, fontScale_, 3, 3, index);
        } else fd = &ite->second;
        uint16_t *outptr = output + fd->leftB + (fontSize_ + fd->iy0) * stride;
        uint8_t *input = &rpData_[fd->rpidx]->pixels[fd->rpy * RECTPACK_WIDTH + fd->rpx];
        int iw = RECTPACK_WIDTH - fd->w;
        int ow = stride - fd->w;
        for (int j = fd->h; j; j--) {
            for (int i = fd->w; i; i--) {
#ifdef TTF_NOALPHA
                *outptr++ = depthColor_[*input++];
#else
                uint16_t c = *outptr;
                if (c == 0) { *outptr++ = depthColor_[*input++]; continue; }
                uint8_t n = *input++;
#ifndef USE_BGR15
                *outptr++ = ((((c >> 11u) * (0xFFu - n) + 0x1Fu * n) / 0xFFu) << 11u)
                    | (((((c >> 5u) & 0x3Fu) * (0xFFu - n) + 0x3Fu * n) / 0xFFu) << 5u)
                    | (((c & 0x1Fu) * (0xFFu - n) + 0x1Fu * n) / 0xFFu);
#else
	            *outptr++ = ((((c >> 10u) * (0xFFu - n) + 0x1Fu * n) / 0xFFu) << 10u)
	                        | (((((c >> 5u) & 0x1Fu) * (0xFFu - n) + 0x1Fu * n) / 0xFFu) << 5u)
	                        | (((c & 0x1Fu) * (0xFFu - n) + 0x1Fu * n) / 0xFFu);
#endif
#endif
            }
            outptr += ow;
            input += iw;
        }
        output += std::max(fd->advW, static_cast<uint8_t>((ch < 1u << 12) ? monoWidth_ : monoWidth_ * 2));
    }
}

void Font::newRectPack() {
    auto *rpd = new RectPackData;
    stbrp_init_target(&rpd->context, RECTPACK_WIDTH, RECTPACK_WIDTH, rpd->nodes, RECTPACK_WIDTH);
    rpData_.push_back(rpd);
}

}
