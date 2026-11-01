#ifdef LILTY_SDL

#include "display/gfx_sdl.h"

#include <SDL2/SDL.h>
#include <string.h>

namespace lilty
{
namespace display
{

namespace
{

// kOn / kOff 是点亮和熄灭时的像素值。不用纯白是因为 128x64 放大 4 倍之后
// 大片白色很刺眼；这点灰度在真机上对应 OLED 的亮暗，观感更接近。
constexpr uint32_t kOn = 0xFFE8E8E8u;
constexpr uint32_t kOff = 0xFF0A0A0Au;

} // namespace

GfxSdl::GfxSdl(int16_t w, int16_t h, int scale) : w_(w), h_(h), scale_(scale > 0 ? scale : 1)
{
    px_ = new uint32_t[size_t(w_) * size_t(h_)];
    memset(px_, 0, sizeof(uint32_t) * size_t(w_) * size_t(h_));
}

GfxSdl::~GfxSdl()
{
    close();
    delete[] px_;
}

bool GfxSdl::begin(const char *title)
{
    if (tex_)
        return true;
    // 示例是用 -D SDL_MAIN_HANDLED 编的（避开 Windows 的 SDL_main/WinMain
    // 那套）；这种情况下 SDL 不知道入口已经就绪，得自己说一声。
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return false;
    win_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w_ * scale_,
                            h_ * scale_, SDL_WINDOW_SHOWN);
    if (!win_)
    {
        close();
        return false;
    }
    // 不用 SDL_RENDERER_PRESENTVSYNC：限帧交给 lilty 自己，否则两处节流叠在
    // 一起，动画会一卡一卡的。
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED);
    if (!ren_)
    {
        close();
        return false;
    }
    // 纹理是 128x64，要放大到窗口大小，取最近邻，方块边缘才不糊。
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w_, h_);
    if (!tex_)
    {
        close();
        return false;
    }
    SDL_RenderClear(ren_);
    SDL_RenderPresent(ren_);
    return true;
}

void GfxSdl::close()
{
    if (tex_)
        SDL_DestroyTexture(tex_);
    if (ren_)
        SDL_DestroyRenderer(ren_);
    if (win_)
        SDL_DestroyWindow(win_);
    tex_ = nullptr;
    ren_ = nullptr;
    win_ = nullptr;
    if (SDL_WasInit(SDL_INIT_VIDEO))
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// 越界必须被忽略，不能断言也不能回绕——驱动是最后一道闸，这条规则对所有
// 后端都一样。
void GfxSdl::setPixel(int16_t x, int16_t y, const gfx::Color &c)
{
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
        return;
    px_[size_t(y) * size_t(w_) + size_t(x)] = gfx::isLit(c) ? kOn : kOff;
}

void GfxSdl::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, const gfx::Color &c)
{
    if (w <= 0 || h <= 0)
        return;
    int16_t x0 = x < 0 ? 0 : x;
    int16_t y0 = y < 0 ? 0 : y;
    int16_t x1 = int16_t(x + w) > w_ ? w_ : int16_t(x + w);
    int16_t y1 = int16_t(y + h) > h_ ? h_ : int16_t(y + h);
    if (x1 <= x0 || y1 <= y0)
        return;
    uint32_t v = gfx::isLit(c) ? kOn : kOff;
    for (int16_t j = y0; j < y1; j++)
    {
        uint32_t *line = px_ + size_t(j) * size_t(w_);
        for (int16_t i = x0; i < x1; i++)
            line[i] = v;
    }
}

void GfxSdl::invertRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (w <= 0 || h <= 0)
        return;
    int16_t x0 = x < 0 ? 0 : x;
    int16_t y0 = y < 0 ? 0 : y;
    int16_t x1 = int16_t(x + w) > w_ ? w_ : int16_t(x + w);
    int16_t y1 = int16_t(y + h) > h_ ? h_ : int16_t(y + h);
    if (x1 <= x0 || y1 <= y0)
        return;
    for (int16_t j = y0; j < y1; j++)
    {
        uint32_t *line = px_ + size_t(j) * size_t(w_);
        for (int16_t i = x0; i < x1; i++)
            line[i] = line[i] == kOn ? kOff : kOn;
    }
}

void GfxSdl::flush()
{
    if (!tex_)
        return;
    // 128x64 只有 32 KB，整块重传比记脏区简单，也不慢。
    SDL_UpdateTexture(tex_, nullptr, px_, int(sizeof(uint32_t) * size_t(w_)));
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    SDL_RenderPresent(ren_);
}

} // namespace display
} // namespace lilty

#endif // LILTY_SDL
