// anim: 用一个共享的弹簧模型驱动框架里所有会动的值，动效手感才一致。
// 对应 lilt 的 anim 包。
#pragma once

#include <stdint.h>

namespace lilty
{
namespace anim
{

// Spring 用 SwiftUI 的方式描述运动：response 是无阻尼振荡周期（秒），
// damping 是临界阻尼的比例（1 不过冲，越小越弹）。
struct Spring
{
    float response;
    float damping;
    // ease 放弃弹簧，改用指数缓动：第一帧就是全速，没有惯性也不会回弹。
    bool ease;
};

// 预设是全局变量，运行时改一处，所有用它的值立刻一起变。
extern Spring springDefault;     // 列表项与布局，轻微过冲
extern Spring springSmooth;      // 整屏进出
extern Spring springSnappy;      // 拇指、进度这类要紧跟的
extern Spring springBouncy;      // 弹出标记，落地带一下
extern Spring springInteractive; // 跟手：按下时用，快且不弹

// timeScale 统一放慢或加快所有动画。
extern float timeScale;

// rubberBand 把原始过卷距离映射成显示距离，结果趋近但永远到不了 dim。
float rubberBand(float x, float dim);

// Value 是朝目标做弹簧运动的 float。零值可以直接用，静止在 0 并跟随
// springDefault。开始动之后不能拷贝，因为调度器持有它的指针。
class Value
{
  public:
    Value()
    {
    }
    // 析构时把自己从调度表里摘掉，否则删节点会留下野指针。
    ~Value();
    Value(const Value &) = delete;
    Value &operator=(const Value &) = delete;

    // get 返回当前显示值，含父值。
    float get() const
    {
        return parent_ ? current_ + parent_->get() : current_;
    }
    // current / target 不含父值。
    float current() const
    {
        return current_;
    }
    float target() const
    {
        return target_;
    }
    float velocity() const
    {
        return velocity_;
    }
    bool settled() const
    {
        return !active_;
    }

    // set 朝 x 动画。
    void set(float x);
    // jump 立刻到 x 并停止运动。
    void jump(float x);
    // shift 在不改目标的前提下平移当前值，之后自己滑回去。布局用它做位置动画。
    void shift(float d);
    // kick 给一个速度，用于列表顶到头的回弹这类反馈。
    void kick(float v);
    // bump 给一个刚好让值荡开 px 再回来的冲量。缓动值会忽略它。
    void bump(float px);

    void setSpring(const Spring *s)
    {
        spring_ = s;
    }
    void setParent(Value *p)
    {
        parent_ = p;
    }
    // setPrecision 设置算作静止的距离；0..1 的进度值要设得更小。
    void setPrecision(float p)
    {
        precision_ = p;
    }

    void stepValue(float dt);

  private:
    void wake();

    float current_ = 0;
    float target_ = 0;
    float velocity_ = 0;
    float precision_ = 0;
    const Spring *spring_ = nullptr;
    Value *parent_ = nullptr;
    bool active_ = false;

    friend bool step(float dt);
};

// step 推进所有在动的值，返回是否有值移动过。
bool step(float dt);
// busy 报告是否还有值在动。
bool busy();

} // namespace anim
} // namespace lilty
