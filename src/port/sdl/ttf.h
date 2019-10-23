#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_Rect SDL_Rect;

namespace TTF {

struct RectPackData;

class Font {
    struct FontData {
        int16_t rpx, rpy;
        uint8_t rpidx;
        int8_t ix0, iy0;
        uint8_t w, h;
        uint8_t advW, leftB;
    };
public:
    explicit Font(const std::string& filename, int size, uint8_t mono_width, SDL_Surface *surface, int index = 0);
    ~Font();

    void render(SDL_Surface *surface, int x, int y, const char *text);

private:
    void newRectPack();

private:
    std::string ttfFilename_;
    int fontSize_ = 0;
    float fontScale_ = 0.f;
    void *font_ = nullptr;
    uint8_t *ttfBuffer_ = nullptr;
    std::unordered_map<int, FontData> fontCache_;
    std::vector<RectPackData*> rpData_;
    uint32_t depthColor_[256];
    uint8_t monoWidth_ = 0;
};

}
