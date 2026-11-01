#include "core/anim.h"

namespace lilty
{
namespace anim
{

namespace
{

constexpr float kTwoPi = 6.2831855f;
// substep 让半隐式欧拉在低帧率下也稳定地解刚度大的弹簧。
constexpr float kSubstep = 1.0f / 240.0f;
constexpr float kE = 2.7182817f;

inline float rate(const Spring &s)
{
    return kTwoPi / s.response;
}

inline float absf(float x)
{
    return x < 0 ? -x : x;
}

// expNeg 近似 e^(-x)（x >= 0）。math.exp 是 float64，ESP32-S3 的 FPU 只有
// 单精度，会把参数减半到足够小，用 (2,2) Padé 逼近，再平方回去。
float expNeg(float x)
{
    if (x <= 0)
        return 1;
    if (x > 16)
        return 0;
    int n = 0;
    while (x > 0.5f)
    {
        x *= 0.5f;
        n++;
    }
    float x2 = x * x / 12.0f;
    float h = x / 2.0f;
    float r = (1 - h + x2) / (1 + h + x2);
    for (; n > 0; n--)
        r *= r;
    return r;
}

// kMaxActive 是在动的值上限；一屏上的节点量远小于它。
constexpr int kMaxActive = 192;
Value *active[kMaxActive];
int activeCount = 0;

} // namespace

Spring springDefault{0.29f, 0.75f, false};
Spring springSmooth{0.39f, 0.85f, false};
Spring springSnappy{0.31f, 0.80f, false};
Spring springBouncy{0.25f, 0.45f, false};
Spring springInteractive{0.14f, 1.00f, false};

float timeScale = 1;

float rubberBand(float x, float dim)
{
    if (dim <= 0)
        return 0;
    bool neg = x < 0;
    if (neg)
        x = -x;
    float y = (1 - 1 / (x * 0.55f / dim + 1)) * dim;
    return neg ? -y : y;
}

void Value::wake()
{
    if (active_)
        return;
    if (activeCount >= kMaxActive)
        return; // 满了就不再新增，宁可不动也不覆盖指针
    active_ = true;
    active[activeCount++] = this;
}

Value::~Value()
{
    if (!active_)
        return;
    for (int i = 0; i < activeCount; i++)
    {
        if (active[i] != this)
            continue;
        active[i] = active[--activeCount];
        active[activeCount] = nullptr;
        return;
    }
}

void Value::set(float x)
{
    if (x == target_)
        return;
    target_ = x;
    wake();
}

void Value::jump(float x)
{
    current_ = target_ = x;
    velocity_ = 0;
}

void Value::shift(float d)
{
    if (d == 0)
        return;
    current_ += d;
    wake();
}

void Value::kick(float v)
{
    velocity_ += v;
    wake();
}

void Value::bump(float px)
{
    const Spring *s = spring_ ? spring_ : &springDefault;
    if (s->ease)
        return;
    kick(px * rate(*s) * kE);
}

void Value::stepValue(float dt)
{
    const Spring &s = spring_ ? *spring_ : springDefault;
    float x = current_ - target_;
    float vel = velocity_;
    if (s.ease)
    {
        x = x * expNeg(rate(s) * dt);
        vel = 0;
    }
    else
    {
        float w = rate(s);
        float k = w * w;
        float c = 2 * s.damping * w;
        int n = int(dt / kSubstep) + 1;
        float h = dt / float(n);
        for (int i = 0; i < n; i++)
        {
            vel += (-k * x - c * vel) * h;
            x += vel * h;
        }
    }
    float p = precision_ == 0 ? 0.05f : precision_;
    if (absf(x) < p && absf(vel) < p * 10)
    {
        current_ = target_;
        velocity_ = 0;
        active_ = false;
        return;
    }
    current_ = target_ + x;
    velocity_ = vel;
}

bool step(float dt)
{
    if (activeCount == 0)
        return false;
    dt *= timeScale;
    for (int i = 0; i < activeCount;)
    {
        Value *v = active[i];
        v->stepValue(dt);
        if (v->active_)
        {
            i++;
            continue;
        }
        active[i] = active[--activeCount];
        active[activeCount] = nullptr;
    }
    return true;
}

bool busy()
{
    return activeCount > 0;
}

} // namespace anim
} // namespace lilty
