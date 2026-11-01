#include "lilty.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <chrono>
#endif

namespace lilty
{

namespace
{

// gLast 是上一帧的时间戳，gMinGap 是帧率上限换算出来的最小间隔。
uint32_t gLast = 0;
uint32_t gMinGap = 0;
// kMaxDt 是一帧最多推进多少秒。卡一下之后不要把动画一次跳完。
constexpr float kMaxDt = 0.1f;

// nowMicros 取单调时钟的微秒数，只用来算差值，回绕交给无符号减法。Arduino
// 下用 micros()，桌面端用 steady_clock——这是整个框架里唯一直接和平台打交
// 道的地方，所以连帧循环都能在 PC 上跑起来做单测。
uint32_t nowMicros()
{
#ifdef ARDUINO
    return micros();
#else
    using namespace std::chrono;
    return uint32_t(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

} // namespace

void setFps(int fps)
{
    gMinGap = fps > 0 ? uint32_t(1000000u / uint32_t(fps)) : 0;
}

void begin(gfx::Screen *screen, ui::Node *first)
{
    if (gMinGap == 0)
        setFps(60);
    ui::boot(screen, first);
    gLast = nowMicros();
}

void loop(void (*hook)(float dt))
{
    uint32_t now = nowMicros();
    uint32_t gap = now - gLast;
    if (gap < gMinGap)
        return;
    gLast = now;
    float dt = float(gap) * 1e-6f;
    if (dt > kMaxDt)
        dt = kMaxDt;
    if (hook)
        hook(dt);
    ui::frame(dt);
}

} // namespace lilty
