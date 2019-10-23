#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

extern "C" {
typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_Rect SDL_Rect;
#ifndef USE_STB_TRUETYPE
typedef struct FT_LibraryRec_ *FT_Library;
typedef struct FT_FaceRec_    *FT_Face;
#endif
}

namespace TTF {

struct RectPackData;

class Font {
    struct FontData {
        int16_t rpx, rpy;
        uint8_t rpidx;

        int8_t ix0, iy0;
        uint8_t w, h;
        uint8_t advW;
    };
	struct FontInfo {
#ifdef USE_STB_TRUETYPE
		float fontScale = 0.f;
		void *font = nullptr;
		uint8_t *ttfBuffer = nullptr;
#else
		FT_Face face = nullptr;
#endif
	};
public:
    Font(int size, uint8_t mono_width, SDL_Surface *surface);
    ~Font();
	bool add(const std::string& filename, int index = 0);

    void render(SDL_Surface *surface, int x, int y, const char *text, bool allowWrap = false, bool shadow = false);

private:
    void newRectPack();

private:
    std::string ttfFilename_;
    int fontSize_ = 0;
    std::vector<FontInfo> fonts_;
    std::unordered_map<uint16_t, FontData> fontCache_;
    std::vector<RectPackData*> rpData_;
    uint32_t depthColor_[256];
    uint8_t monoWidth_ = 0;

#ifndef USE_STB_TRUETYPE
	FT_Library library_;
#endif
};

}
