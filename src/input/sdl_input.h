// sdl_input: 用键盘模拟旋钮和按键，给 PC 上的 SDL 后端配着用。
//
// 没有接口类，约定和其它输入后端一样：每帧调一次 poll，把按键送进 ui::emit。
// 按住不放交给 ui::KeyPoller 去消抖和连发，所以在 PC 上长按方向键同样会先
// 停一下再加速——和真机上转旋钮的手感对得上。
//
// 同样只在 LILTY_SDL 下编译。
#pragma once

#ifdef LILTY_SDL

#include "core/ui.h"

namespace lilty
{
namespace input
{

class SdlKeys
{
  public:
    // poll 每帧调用一次：先把 SDL 的事件队列抽干，再把键盘电平喂给 KeyPoller。
    void poll(float dt);
    // quit 在用户关窗口或按 Ctrl+Q 之后变成 true，主循环据此退出。
    bool quit() const
    {
        return quit_;
    }

  private:
    ui::KeyPoller poller_;
    bool quit_ = false;
};

} // namespace input
} // namespace lilty

#endif // LILTY_SDL
