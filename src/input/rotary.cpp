#include "input/rotary.h"

namespace lilty
{
namespace input
{

namespace
{

// 只有编码器的两个引脚需要中断，所以解码状态放在文件级就够了。
int gPinA = -1;
int gPinB = -1;
// gPrev 是上一次采到的 AB 电平，gAccum 是这个格子已经走过的四分之一步数。
volatile uint8_t gPrev = 0;
volatile int8_t gAccum = 0;
volatile int32_t gDetents = 0;

// 正交解码表：(上一状态 << 2 | 当前状态) 对应的方向，0 表示没动或者跳变。
const int8_t kQuad[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR encoderIsr()
{
    uint8_t a = uint8_t(digitalRead(gPinA) ? 1 : 0);
    uint8_t b = uint8_t(digitalRead(gPinB) ? 1 : 0);
    uint8_t cur = uint8_t((a << 1) | b);
    int8_t d = kQuad[uint8_t(((gPrev << 2) | cur) & 0x0F)];
    gPrev = cur;
    if (d == 0)
        return;
    int8_t acc = int8_t(gAccum + d);
    if (acc >= 4)
    {
        gAccum = int8_t(acc - 4);
        gDetents++;
    }
    else if (acc <= -4)
    {
        gAccum = int8_t(acc + 4);
        gDetents--;
    }
    else
    {
        gAccum = acc;
    }
}

// takeDetents 取走累计的格子数。
int32_t takeDetents()
{
    noInterrupts();
    int32_t d = gDetents;
    gDetents = 0;
    interrupts();
    return d;
}

} // namespace

void Rotary::begin(int pinA, int pinB, int pinButton, int pinBack)
{
    gPinA = pinA;
    gPinB = pinB;
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
    gPrev = uint8_t(((digitalRead(pinA) ? 1 : 0) << 1) | (digitalRead(pinB) ? 1 : 0));
    gAccum = 0;
    gDetents = 0;
    attachInterrupt(digitalPinToInterrupt(pinA), encoderIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pinB), encoderIsr, CHANGE);
    pinButton_ = pinButton;
    pinBack_ = pinBack;
    if (pinButton_ >= 0)
        pinMode(pinButton_, INPUT_PULLUP);
    if (pinBack_ >= 0)
        pinMode(pinBack_, INPUT_PULLUP);
}

void Rotary::poll(float dt)
{
    int32_t d = takeDetents();
    if (reversed_)
        d = -d;
    if (d != 0)
    {
        ui::Key k = d > 0 ? ui::Key::Down : ui::Key::Up;
        // 一帧最多走几步，免得手一甩焦点就飞了。
        int32_t steps = d > 0 ? d : -d;
        if (steps > 4)
            steps = 4;
        for (int32_t i = 0; i < steps; i++)
        {
            ui::emit(k, ui::Press);
            ui::emit(k, ui::Release);
        }
    }
    if (pinButton_ >= 0)
        poller_.poll(ui::Key::OK, digitalRead(pinButton_) == LOW, dt);
    if (pinBack_ >= 0)
        poller_.poll(ui::Key::Back, digitalRead(pinBack_) == LOW, dt);
}

} // namespace input
} // namespace lilty
