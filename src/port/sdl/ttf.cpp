#include "ttf.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#ifdef USE_STB_TRUETYPE
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <fstream>
#else
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <SDL.h>

#include <climits>

namespace TTF {

const int RECTPACK_WIDTH = 1024;

struct RectPackData {
    stbrp_context context;
    stbrp_node nodes[RECTPACK_WIDTH];
    uint8_t pixels[RECTPACK_WIDTH * RECTPACK_WIDTH];
};

Font::Font(int size, uint8_t mono_width, SDL_Surface *surface): fontSize_(size), monoWidth_(mono_width) {
#ifndef USE_STB_TRUETYPE
	FT_Init_FreeType(&library_);
#endif

    for (uint16_t i = 0; i < 256; ++i) {
        uint8_t c;
#ifndef TTF_SMOOTH
        if (i < 128) c = i * 3 / 2;
        else c = i / 2 + 128;
#else
        c = i;
#endif
        depthColor_[i] = SDL_MapRGB(surface->format, c, c, c);
    }
}

Font::~Font() {
	for (auto *&p: rpData_) delete p;
	rpData_.clear();
	for (auto &p: fonts_) {
#ifdef USE_STB_TRUETYPE
		delete static_cast<stbtt_fontinfo *>(p.font);
		delete p.ttfBuffer;
#else
		FT_Done_Face(p.face);
#endif
	}
#ifndef USE_STB_TRUETYPE
	FT_Done_FreeType(library_);
#endif
}

bool Font::add(const std::string &filename, int index) {
	FontInfo fi;
#ifdef USE_STB_TRUETYPE
	auto *info = new stbtt_fontinfo;
	std::ifstream fin(filename, std::ios::binary);
    if (fin.fail()) return false;
	fin.seekg(0, std::ios::end);
	size_t sz = fin.tellg();
	fi.ttfBuffer = new uint8_t[sz];
	fin.seekg(0, std::ios::beg);
	fin.read(reinterpret_cast<char*>(fi.ttfBuffer), sz);
	fin.close();
	stbtt_InitFont(info, fi.ttfBuffer, stbtt_GetFontOffsetForIndex(fi.ttfBuffer, index));
	fi.fontScale = stbtt_ScaleForMappingEmToPixels(info, static_cast<float>(fontSize_));
	fi.font = info;
#else
	if (FT_New_Face(library_, filename.c_str(), index, &fi.face)) return false;
	FT_Set_Pixel_Sizes(fi.face, 0, fontSize_);
#endif
	fonts_.push_back(fi);
	newRectPack();
	return true;
}

/* UTF-8 to UCS-4 */
static inline uint32_t utf8toucs4(const char *&text) {
    uint8_t c = static_cast<uint8_t>(*text);
    if (c < 0x80) {
        uint16_t ch = c;
        ++text;
        return ch;
    } else if (c < 0xC0) {
    	++text;
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
    ++text;
    return 0;
}

void Font::render(SDL_Surface *surface, int x, int y, const char *text, bool allowWrap, bool shadow) {
    int stride = surface->pitch / surface->format->BytesPerPixel;
    int surface_w = surface->w;

    while (*text != 0) {
        uint32_t ch = utf8toucs4(text);
        if (ch == 0 || ch > 0xFFFFu) continue;

        /* Check if bitmap is already cached */
        FontData *fd;
        auto ite = fontCache_.find(ch);

        if (ite == fontCache_.end()) {
	        FontInfo *fi = NULL;
#ifdef USE_STB_TRUETYPE
	        stbtt_fontinfo *info;
#endif
	        uint32_t index = 0;
	        for (auto &f: fonts_) {
	        	fi = &f;
#ifdef USE_STB_TRUETYPE
		        info = static_cast<stbtt_fontinfo*>(f.font);
		        index = stbtt_FindGlyphIndex(info, ch);
		        if (index == 0) continue;
#else
		        index = FT_Get_Char_Index(f.face, ch);
		        if (index == 0) continue;
		        if (!FT_Load_Glyph(f.face, index, FT_LOAD_DEFAULT)) break;
#endif
	        }
            fd = &fontCache_[ch];
	        if (fi == NULL) {
	            memset(fd, 0, sizeof(FontData));
	            continue;
	        }

#ifdef USE_STB_TRUETYPE
            /* Read font data to cache */
            int advW, leftB;
            stbtt_GetGlyphHMetrics(info, index, &advW, &leftB);
            fd->advW = static_cast<uint8_t>(fi->fontScale * advW);
            leftB = static_cast<uint8_t>(fi->fontScale * leftB);
            int ix0, iy0, ix1, iy1;
            stbtt_GetGlyphBitmapBoxSubpixel(info, index, fi->fontScale, fi->fontScale, 3, 3, &ix0, &iy0, &ix1, &iy1);
            fd->ix0 = leftB;
            fd->iy0 = iy0;
            fd->w = ix1 - ix0;
            fd->h = iy1 - iy0;
#else
            unsigned char *src_ptr;
            int bitmap_pitch;
            if (FT_Render_Glyph(fi->face->glyph, FT_RENDER_MODE_NORMAL)) break;
            FT_GlyphSlot slot = fi->face->glyph;
            fd->ix0 = slot->bitmap_left;
            fd->iy0 = -slot->bitmap_top;
            fd->w = slot->bitmap.width;
            fd->h = slot->bitmap.rows;
            fd->advW = slot->advance.x >> 6;
            src_ptr = slot->bitmap.buffer;
            bitmap_pitch = slot->bitmap.pitch;
#endif

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

#ifdef USE_STB_TRUETYPE
            stbtt_MakeGlyphBitmapSubpixel(info, &rpd->pixels[rc.y * RECTPACK_WIDTH + rc.x], fd->w, fd->h, RECTPACK_WIDTH, fi->fontScale, fi->fontScale, 3, 3, index);
#else
            auto *dst_ptr = &rpd->pixels[rc.y * RECTPACK_WIDTH + rc.x];
			for (int k = 0; k < fd->h; ++k) {
				memcpy(dst_ptr, src_ptr, fd->w);
				src_ptr += bitmap_pitch;
				dst_ptr += RECTPACK_WIDTH;
			}
#endif
        } else {
            fd = &ite->second;
            if (fd->advW == 0) continue;
        }
        int cwidth = std::max(fd->advW, static_cast<uint8_t>((ch < (1u << 12u)) ? monoWidth_ : monoWidth_ * 2));
        if (x + cwidth > surface_w) {
            if (!allowWrap) break;
            x = 0;
            y += fontSize_;
            if (y + fontSize_ > surface->h)
                break;
        }
        uint16_t *outptr = static_cast<uint16_t*>(surface->pixels) + stride * (y + fontSize_ + fd->iy0) + x + fd->ix0;
        uint8_t *input = &rpData_[fd->rpidx]->pixels[fd->rpy * RECTPACK_WIDTH + fd->rpx];
        int iw = RECTPACK_WIDTH - fd->w;
        int ow = stride - fd->w;
        for (int j = fd->h; j; j--) {
            for (int i = fd->w; i; i--) {
#define TTF_NOALPHA
#ifdef TTF_NOALPHA
                uint8_t c;
                if ((c = *input++) >= 32) {
                    *outptr++ = depthColor_[c];
                    if (shadow)
                        *(outptr + stride) = 0;
                } else
                    ++outptr;
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
        x += cwidth;
    }
}

void Font::newRectPack() {
    auto *rpd = new RectPackData;
    stbrp_init_target(&rpd->context, RECTPACK_WIDTH, RECTPACK_WIDTH, rpd->nodes, RECTPACK_WIDTH);
    rpData_.push_back(rpd);
}

}
