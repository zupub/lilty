// gfx_sdl: 把 128x64 的单色画面画进一个 SDL2 窗口，用来在 PC 上试界面。
//
// 和其它驱动一样，它只实现 gfx::Screen 的那几个虚函数，所以界面代码一行都
// 不用改。窗口按整数倍放大（默认 4 倍，512x256），一个逻辑像素就是一个实心
// 方块，放大后边缘仍然是利的。
//
// 只有定义了 LILTY_SDL 时这个文件才有内容，于是 ESP32 构建根本看不到
// <SDL2/SDL.h>；没开这个宏的 native 构建（比如 pio test -e native）也不用装
// SDL 就能编译。
#pragma once

#ifdef LILTY_SDL

#include <stdint.h>

#include "core/gfx.h"

// 头文件里不拖 SDL 进来，只前向声明。SDL2 里这几个本来就是不透明指针。
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace lilty
{
namespace display
{

class GfxSdl : public gfx::Screen
{
  public:
    // scale 是窗口相对逻辑像素的放大倍数，小于 1 会被当成 1。
    explicit GfxSdl(int16_t w = 128, int16_t h = 64, int scale = 4);
    ~GfxSdl() override;

    // 持有 SDL 句柄和一块像素缓冲，拷贝没有意义。
    GfxSdl(const GfxSdl &) = delete;
    GfxSdl &operator=(const GfxSdl &) = delete;

    // begin 建窗口、渲染器和纹理。返回 false 表示 SDL 没起来，通常是没装
    // 图形驱动，或者程序跑在没有显示器的环境里。
    bool begin(const char *title = "lilty");
    // close 释放窗口。析构里会自动调，手动调也可以。
    void close();
    // ready 报告窗口是否已经建好。
    bool ready() const
    {
        return tex_ != nullptr;
    }

    int16_t width() const override
    {
        return w_;
    }
    int16_t height() const override
    {
        return h_;
    }
    void setPixel(int16_t x, int16_t y, const gfx::Color &c) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, const gfx::Color &c) override;
    // 单色屏反色就是翻转像素，代价和画一遍一样，所以直接支持——光标因此
    // 用反色绘制，和真机上看到的效果一致。
    bool canInvert() const override
    {
        return true;
    }
    void invertRect(int16_t x, int16_t y, int16_t w, int16_t h) override;
    void flush() override;

  private:
    int16_t w_, h_;
    int scale_;
    // px_ 是 ARGB8888 的整屏缓冲，和 SDL 的纹理格式一一对应。
    uint32_t *px_ = nullptr;
    SDL_Window *win_ = nullptr;
    SDL_Renderer *ren_ = nullptr;
    SDL_Texture *tex_ = nullptr;
};

} // namespace display
} // namespace lilty

#endif // LILTY_SDL
