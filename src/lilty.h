// lilty: 把 gfx / anim / ui / widget 粘到一帧上，给你一个 begin + loop。
// 名字是 lilt 的 C++ 移植——lilt 本身是 Go 写的（https://github.com/chhongzh/lilt），
// 这里只对应它的包结构。
#pragma once

#include "core/gfx.h"
#include "core/ui.h"
#include "core/widget.h"

namespace lilty
{

// setFps 限制帧率上限，0 表示不限制。默认 60。
void setFps(int fps);
// begin 绑定屏幕并把 first 作为根页面推进层栈。要在此之前调 ui::setZoom。
void begin(gfx::Screen *screen, ui::Node *first);
// loop 每帧调用一次：量出时间、跑 hook、推进一帧。
// hook 用来采样按键，比 ui::frame 先跑，于是这一帧的输入立刻生效。
void loop(void (*hook)(float dt) = nullptr);

} // namespace lilty
