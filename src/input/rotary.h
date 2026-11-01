// rotary: 旋转编码器（正交解码 + 自带按键）和独立按键的输入适配。
//
// 编码器用中断解码，每 4 个四分之一步算一个格子。lilty 的手感是“按下只反
// 馈、松手才动”，而转一格没有松手可言，所以每个格子合成一次按下加一次
// 松手：转一格就走一步，快速连转就是连续移动。
#pragma once

#include <Arduino.h>

#include "core/ui.h"

namespace lilty
{
namespace input
{

class Rotary
{
  public:
    // begin 绑定引脚。按键按“另一端接 GND、打开内部上拉”处理，back 传 -1
    // 表示没有这个键。
    void begin(int pinA, int pinB, int pinButton, int pinBack = -1);
    // setReversed 交换旋转方向。
    void setReversed(bool r)
    {
        reversed_ = r;
    }
    // poll 每帧调用一次，dt 是距上一帧的秒数。
    void poll(float dt);

  private:
    ui::KeyPoller poller_;
    bool reversed_ = false;
    int pinButton_ = -1;
    int pinBack_ = -1;
};

} // namespace input
} // namespace lilty
