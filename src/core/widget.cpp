// widget 的实现。
//
// 与原版的区别集中在一处：所有处理器都同步跑完，因此 Control 的 busy 只会
// 在自己那次调用里为真，“加载中”只在按钮按住确认时才有可见意义。
#include "core/widget.h"

#include <math.h>
#include <string.h>

namespace lilty
{
namespace widget
{

// ------------------------------------------------------------- 小工具

static float clampf(float x, float lo, float hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static float maxf(float a, float b)
{
    return a > b ? a : b;
}

static void copyText(char *dst, const char *src)
{
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    int i = 0;
    while (src[i] && i < kTextCap - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

// chain 先跑 a 再跑 b，遇到失败就停；任一为空时退化成另一个。
static Action chain(const Action &a, const Action &b)
{
    if (!a)
        return b;
    if (!b)
        return a;
    return [a, b]() {
        if (!a())
            return false;
        return b();
    };
}

// changeIntAction 把“整数值变化”处理器包成一次动作。
static Action changeIntAction(const std::function<bool(int, int)> &h, int from, int to)
{
    if (!h || from == to)
        return Action();
    return [h, from, to]() { return h(from, to); };
}

// changeBoolAction 是布尔版本。
static Action changeBoolAction(const std::function<bool(bool, bool)> &h, bool from, bool to)
{
    if (!h || from == to)
        return Action();
    return [h, from, to]() { return h(from, to); };
}

void shake(Node *d)
{
    d->offsetX()->kick(-260);
}

void Control::tick(float dt)
{
    if (!busy_)
        return;
    elapsed_ += dt;
    if (elapsed_ >= kLoadingDelay)
        ui::invalidate();
}

bool Control::run(const Action &call, const Done &done)
{
    if (busy_)
        return false;
    if (!call)
    {
        if (done)
            done(true);
        return true;
    }
    busy_ = true;
    elapsed_ = 0;
    bool ok = call();
    // done 可能把 this 所在的层关掉，所以先收尾再回调。
    busy_ = false;
    elapsed_ = 0;
    if (done)
        done(ok);
    return true;
}

// drawDots 在物理坐标 cx, cy 周围画三个轮流跳的点。
static void drawDots(gfx::Canvas &c, int16_t cx, int16_t cy, float t, const gfx::Color &col)
{
    int16_t gap = ui::dp(5), r = ui::dp(1);
    for (int i = -1; i <= 1; i++)
    {
        float phase = t * 7 - float(i) * 0.8f;
        float s = sinf(phase);
        if (s < 0)
            s = -s;
        int16_t lift = ui::dp(2 * s);
        c.fillCircle(int16_t(cx + i * gap), int16_t(cy - lift), r, col);
    }
}

// drawWaiting 清出一小块底色再画点，给文字可能压在下面的行和字段用。
static void drawWaiting(gfx::Canvas &c, int16_t cx, int16_t cy, float t)
{
    int16_t w = ui::dp(18), h = ui::dp(7);
    c.fillRect(gfx::Rect{int16_t(cx - w / 2), int16_t(cy - h / 2 - ui::dp(1)), w, h}, ui::theme.background);
    drawDots(c, cx, int16_t(cy + ui::dp(1)), t, ui::theme.foreground);
}

// ---------------------------------------------------------------- Label

// 跑马灯的速度、两端的停留时间，以及末字之后留空的宽度。
static constexpr float kMarqueeSpeed = 24;
static constexpr float kMarqueePause = 1;
static constexpr float kMarqueeGap = 2;

Label::Label(const char *text, std::initializer_list<Option> opts)
{
    copyText(text_, text);
    remeasure();
    ui::apply(this, opts);
}

const gfx::Font *Label::font() const
{
    return font_ ? font_ : ui::theme.font;
}

void Label::setText(const char *s)
{
    if (!s)
        s = "";
    if (strcmp(s, text_) == 0)
        return;
    copyText(text_, s);
    remeasure();
    restart();
    ui::relayout();
}

void Label::setFont(const gfx::Font *f)
{
    font_ = f;
    remeasure();
    ui::relayout();
}

void Label::setColor(const gfx::Color &c)
{
    color_ = c;
    hasColor_ = true;
    ui::invalidate();
}

void Label::setAlign(float x)
{
    align_ = x;
    ui::invalidate();
}

void Label::setMarquee(Marquee m)
{
    marquee_ = m;
    restart();
}

void Label::remeasure()
{
    textW_ = ui::textWidth(font(), text_);
}

void Label::restart()
{
    step_ = HoldStart;
    timer_ = 0;
    scroll_.set(0);
}

float Label::overflow() const
{
    return textW_ + kMarqueeGap - contentRect().w;
}

bool Label::scrolling() const
{
    if (marquee_ == MarqueeAlways)
        return true;
    Node *f = ui::focused();
    return f != nullptr && ui::isAncestor(const_cast<Label *>(this), f);
}

float Label::shift(float over) const
{
    if (align_ > 0.5f)
        return over - scroll_.get();
    return scroll_.get();
}

Vec2 Label::measure(Vec2 max)
{
    float w = textW_ < max.x ? textW_ : max.x;
    return Vec2{w, ui::lineHeight(font())};
}

void Label::update(float dt)
{
    float over = overflow();
    if (over <= kMarqueeGap || !scrolling())
    {
        if (step_ != HoldStart || timer_ != 0)
            restart();
        return;
    }
    timer_ += dt;
    switch (step_)
    {
    case HoldStart:
        if (timer_ >= kMarqueePause)
        {
            step_ = Forward;
            timer_ = 0;
        }
        break;
    case Forward: {
        float x = timer_ * kMarqueeSpeed;
        if (x >= over)
        {
            x = over;
            step_ = HoldEnd;
            timer_ = 0;
        }
        scroll_.jump(x);
        ui::invalidate();
        break;
    }
    case HoldEnd:
        if (timer_ >= kMarqueePause)
        {
            step_ = Back;
            timer_ = 0;
            scroll_.set(0);
        }
        break;
    case Back:
    default:
        if (scroll_.settled())
        {
            step_ = HoldStart;
            timer_ = 0;
        }
        break;
    }
}

void Label::draw(gfx::Canvas &c)
{
    gfx::Rect box = contentRect().pixels();
    gfx::Canvas clip = c.clip(box);
    const gfx::Font *f = font();
    gfx::Color col = hasColor_ ? color_ : ui::theme.foreground;
    int16_t textPx = ui::dp(textW_);
    int16_t x = int16_t(box.x + ui::round16(align_ * float(box.w - textPx)));
    float over = overflow();
    if (over > kMarqueeGap)
        x = int16_t(box.x - ui::dp(shift(over)));
    int16_t y = int16_t(box.y + (box.h - gfx::lineHeight(f)) / 2 + gfx::ascent(f));
    clip.text(f, x, y, text_, col);
}

// ---------------------------------------------------------------- Scroll

// 一次按键滚动的距离、露出焦点时留的余量、波浪最多再拖慢多少层。
static constexpr float kScrollStep = 16;
static constexpr float kRevealMargin = 2;

// gWave[k] 移动离焦点 k 步的子节点。每次滚动都从 springDefault 重新派生，
// 于是改预设仍然生效。
static anim::Spring gWave[kWaveSteps];

Scroll::Scroll(Node *content, Axes axes, std::initializer_list<Option> opts) : content_(content), axes_(axes)
{
    setClip(true);
    thumb_[0].setSpring(&anim::springSnappy);
    thumb_[1].setSpring(&anim::springSnappy);
    ui::attach(this, content_);
    ui::apply(this, opts);
}

Vec2 Scroll::measureContent(Vec2 max)
{
    if (on(0))
        max.x = ui::unbounded;
    if (on(1))
        max.y = ui::unbounded;
    return ui::measureNode(content_, max);
}

Vec2 Scroll::measureChildren(Vec2 max)
{
    Vec2 c = measureContent(max);
    return Vec2{c.x < max.x ? c.x : max.x, c.y < max.y ? c.y : max.y};
}

void Scroll::arrangeChildren(Rect r)
{
    Vec2 c = measureContent(Vec2{r.w, r.h});
    float w = r.w, h = r.h;
    if (on(0))
        w = maxf(c.x, r.w);
    if (on(1))
        h = maxf(c.y, r.h);
    ui::arrange(content_, Rect{r.x, r.y, w, h});
    view_[0] = r.w;
    view_[1] = r.h;
    limit_[0] = w - r.w;
    limit_[1] = h - r.h;
    for (int i = 0; i < 2; i++)
        pos_[i] = clampf(pos_[i], 0, limit_[i]);
    apply();
}

void Scroll::apply()
{
    for (int i = 0; i < 2; i++)
    {
        if (!on(i))
            continue;
        float to = -pos_[i] + anim::rubberBand(pull_[i], view_[i]);
        anim::Value *o = offset(i);
        float d = to - o->target();
        if (d != 0)
        {
            o->jump(to);
            wave(i, d);
        }
        thumb_[i].set(-to);
    }
    ui::invalidate();
}

void Scroll::wave(int i, float d)
{
    int kids = 0;
    for (Node *c = content_->firstChild(); c; c = c->nextSibling())
        kids++;
    if (kids == 0)
    {
        anim::Value *o = offset(i);
        o->setSpring(&anim::springDefault);
        o->shift(-d);
        return;
    }
    const anim::Spring &base = anim::springDefault;
    for (int k = 0; k < kWaveSteps; k++)
    {
        gWave[k].response = base.response * (1 + stagger_ * float(k));
        gWave[k].damping = base.damping;
        gWave[k].ease = base.ease;
    }
    int focus = focusedChild();
    int n = 0;
    for (Node *c = content_->firstChild(); c; c = c->nextSibling(), n++)
    {
        int k = 0;
        if (focus >= 0)
        {
            int dist = n > focus ? n - focus : focus - n;
            k = dist < kWaveSteps ? dist : kWaveSteps - 1;
        }
        anim::Value *o = i == 0 ? c->offsetX() : c->offsetY();
        o->setSpring(&gWave[k]);
        o->shift(-d);
    }
}

int Scroll::focusedChild() const
{
    Node *f = ui::focused();
    if (!f)
        return -1;
    int n = 0;
    for (Node *c = content_->firstChild(); c; c = c->nextSibling(), n++)
    {
        if (ui::isAncestor(c, f))
            return n;
    }
    return -1;
}

Rect Scroll::contentRest(Node *d) const
{
    Rect r = d->rect();
    for (Node *n = d->parent(); n && n != content_; n = n->parent())
    {
        r.x += n->rect().x + n->offsetX()->target();
        r.y += n->rect().y + n->offsetY()->target();
    }
    r.x += d->offsetX()->target();
    r.y += d->offsetY()->target();
    return r;
}

void Scroll::reveal(Node *d)
{
    if (!ui::isAncestor(content_, d))
        return;
    Rect r = contentRest(d);
    for (int i = 0; i < 2; i++)
    {
        if (!on(i))
            continue;
        float lo = i == 0 ? r.x : r.y;
        float size = i == 0 ? r.w : r.h;
        if (lo < pos_[i])
            pos_[i] = lo - kRevealMargin;
        else if (lo + size > pos_[i] + view_[i])
            pos_[i] = lo + size - view_[i] + kRevealMargin;
        pos_[i] = clampf(pos_[i], 0, limit_[i]);
        pull_[i] = 0;
    }
    apply();
}

bool Scroll::alongside(Node *d, int i) const
{
    Rect sr = ui::restRect(const_cast<Scroll *>(this));
    Rect dr = ui::restRect(d);
    int j = 1 - i;
    float lo = j == 0 ? sr.x : sr.y;
    float size = j == 0 ? sr.w : sr.h;
    float dlo = j == 0 ? dr.x : dr.y;
    float dsize = j == 0 ? dr.w : dr.h;
    return dlo < lo + size && lo < dlo + dsize;
}

bool Scroll::outerRoom(int i, float sign)
{
    for (Node *n = parent(); n; n = n->parent())
    {
        if (n->nodeTag() != ui::kTagScroll)
            continue;
        Scroll *o = static_cast<Scroll *>(n);
        if (o->on(i) && o->room(i, sign) > 0.5f)
            return true;
    }
    return false;
}

float Scroll::room(int i, float sign) const
{
    if (sign > 0)
        return limit_[i] - pos_[i];
    return pos_[i];
}

// arrowScroll 在内容到头时把内容朝按住的方向拉开，松手放开；一次点按在松
// 手时走一步，按住则在每次连发走一步。
bool Scroll::arrowScroll(KeyEvent &e, Node *next)
{
    int i;
    float sign;
    switch (e.key)
    {
    case Key::Left:
        i = 0;
        sign = -1;
        break;
    case Key::Right:
        i = 0;
        sign = 1;
        break;
    case Key::Up:
        i = 1;
        sign = -1;
        break;
    default:
        i = 1;
        sign = 1;
        break;
    }
    if (!on(i))
        return false;
    if (next != nullptr && !ui::isAncestor(content_, next) && !alongside(next, i))
    {
        // 从一排横着走到另一排会斜着跳，把它当成本排的尽头更合手。
        next = nullptr;
    }
    bool pulled = pull_[i] != 0;
    if (e.phase == ui::Release && pulled)
    {
        pull_[i] = 0;
        apply();
    }
    if (!e.fires())
    {
        if (e.phase == ui::Press && next == nullptr && room(i, sign) <= 0.5f && !outerRoom(i, sign))
        {
            pull_[i] = -sign * kScrollStep;
            apply();
            return true;
        }
        return false;
    }
    if (pulled && e.phase == ui::Release)
    {
        // 这次点按就是朝尽头推了一下，按下时已经表现过了。
        return true;
    }
    bool inside = next != nullptr && ui::isAncestor(content_, next);
    // 目标在半个屏幕之外时会跳过不可聚焦的内容，例如一大段文字，所以改成
    // 逐步滚动。
    if (inside)
    {
        Rect nr = contentRest(next);
        float lo = i == 0 ? nr.x : nr.y;
        float size = i == 0 ? nr.w : nr.h;
        bool near = lo + size > pos_[i] - view_[i] / 2 && lo < pos_[i] + view_[i] * 1.5f;
        if (near)
        {
            pull_[i] = 0;
            return false;
        }
    }
    float rm = room(i, sign);
    if (rm > 0.5f)
    {
        pos_[i] += sign * (rm < kScrollStep ? rm : kScrollStep);
        apply();
        return true;
    }
    if (next != nullptr || outerRoom(i, sign))
        return false;
    pull_[i] -= sign * kScrollStep;
    apply();
    return true;
}

// thumbGeom 给出滚动条的起点和长度；过卷时长度会缩短。
static void thumbGeom(float p, float limit, float view, float &lo, float &size)
{
    size = maxf(view * view / (view + limit), 4);
    float over = 0;
    if (p < 0)
        over = -p;
    else if (p > limit)
        over = p - limit;
    size = maxf(size - over, 2);
    lo = clampf(p / limit, 0, 1) * (view - size);
}

void Scroll::drawOver(gfx::Canvas &c)
{
    if (hideBar_)
        return;
    Rect r = rect();
    Rect in = contentRect();
    gfx::Color col = ui::theme.foreground;
    // 滚动条贴在远侧的边上，有边框时再往里让一像素。
    float edge = bordered() ? 2.0f : 1.0f;
    if (on(1) && limit_[1] > 0)
    {
        float lo, size;
        thumbGeom(thumb_[1].get(), limit_[1], view_[1], lo, size);
        c.vline(ui::dp(r.w - edge), ui::dp(in.y + lo), ui::dp(size), col);
    }
    if (on(0) && limit_[0] > 0)
    {
        float lo, size;
        thumbGeom(thumb_[0].get(), limit_[0], view_[0], lo, size);
        c.hline(ui::dp(in.x + lo), ui::dp(r.h - edge), ui::dp(size), col);
    }
}

// ---------------------------------------------------------------- List

List::List(std::initializer_list<Option> opts)
    : Scroll(new Flex(ui::Vertical), ScrollVertical), column_(static_cast<Flex *>(content()))
{
    setWidth(ui::rel(1));
    setHeight(ui::rel(1));
    // 给滚动条留位置，免得它压在光标下面。
    setPadding(ui::ltrb(0, 0, 3, 0));
    ui::apply(this, opts);
}

// -------------------------------------------------------------- ListItem

ListItem::ListItem(const char *title, std::initializer_list<Option> opts) : Flex(ui::Vertical)
{
    init(title);
    ui::apply(this, opts);
}

void ListItem::init(const char *title)
{
    title_ = new Label(title);
    title_->setGrow(1);
    ui::attach(this, title_);
    setFocusable(true);
    setClip(true);
    setPadding(ui::theme.rowPadding);
    setGap(4);
    setCrossAlign(ui::CrossCenter);
    offsetX()->setSpring(&anim::springBouncy);
}

void ListItem::setTrailing(Node *d)
{
    if (trailing_)
    {
        ui::remove(trailing_);
        delete trailing_;
        trailing_ = nullptr;
    }
    trailing_ = d;
    if (!d)
        return;
    d->offsetY()->setSpring(&anim::springSnappy);
    ui::attach(this, d);
}

// handleKey 在松手时激活，和 iOS 一样：按下只挤压光标，按下后变成拖动的
// 那次什么也不做。
bool ListItem::handleKey(KeyEvent &e)
{
    if (e.key != Key::OK)
        return false;
    if (e.phase == ui::Release && !e.canceled && !busy())
    {
        if (activate)
        {
            activate();
        }
        else
        {
            run(onClick, [this](bool ok) {
                if (!ok)
                    shake(this);
            });
        }
    }
    return true;
}

// update 在等待时把尾部控件向上滑出，动作返回后再从下方滑回来。
void ListItem::update(float dt)
{
    Control::tick(dt);
    if (loading() == waiting_)
        return;
    waiting_ = !waiting_;
    if (!trailing_)
        return;
    anim::Value *o = trailing_->offsetY();
    float h = rect().h;
    if (waiting_)
    {
        o->set(-h);
    }
    else
    {
        o->jump(h);
        o->set(0);
    }
}

void ListItem::drawOver(gfx::Canvas &c)
{
    if (!waiting_)
        return;
    Rect r = contentRect();
    drawWaiting(c, ui::dp(r.right() - 9), ui::dp(r.center().y), elapsed_);
}

// ---------------------------------------------------------------- Button

// kResultHold 是成功或失败标记停留多久，之后标签回来。
static constexpr float kResultHold = 1.2f;

Button::Button(const char *text, std::initializer_list<Option> opts)
{
    label_ = new Label(text);
    label_->setAnchor(ui::Center);
    label_->setOrigin(ui::Center);
    label_->offsetY()->setSpring(&anim::springSnappy);
    ui::attach(this, label_);
    const ui::Theme &t = ui::theme;
    setFocusable(true);
    setClip(true);
    setPadding(t.buttonPadding);
    setBorder(t.foreground);
    setRadius(t.radius);
    offsetX()->setSpring(&anim::springBouncy);
    progress_.setSpring(&anim::springSnappy);
    progress_.setPrecision(0.002f);
    result_.setSpring(&anim::springSnappy);
    result_.setPrecision(0.01f);
    ui::apply(this, opts);
}

bool Button::handleKey(KeyEvent &e)
{
    if (e.key != Key::OK)
        return false;
    if (e.phase == ui::Press)
    {
        if (state_ == Idle && !busy() && hold_ > 0)
        {
            holding_ = true;
            fired_ = false;
        }
        return true;
    }
    if (e.phase != ui::Release)
        return true;
    if (hold_ == 0)
    {
        if (!e.canceled && state_ == Idle)
            fire();
    }
    else if (fired_)
    {
        // 处理器已经跑完了，把光标放回来。
        if (!busy() && state_ == Idle)
            progress_.set(0);
    }
    else
    {
        progress_.set(0);
    }
    holding_ = false;
    fired_ = false;
    return true;
}

void Button::fire()
{
    run(onClick, [this](bool ok) { done(ok); });
}

void Button::done(bool ok)
{
    bool waited = state_ == Loading;
    state_ = Idle;
    if (!ok)
    {
        state_ = Failed;
        shake(this);
    }
    else if (waited)
    {
        state_ = Succeeded;
    }
    if (state_ == Idle)
    {
        if (!holding_)
            progress_.set(0);
    }
    else
    {
        timer_ = 0;
        mark_ = state_;
        label_->offsetY()->set(-rect().h);
        result_.jump(0);
        result_.set(1);
    }
    if (after)
        after(ok);
}

void Button::update(float dt)
{
    Control::tick(dt);
    if (holding_ && !fired_)
    {
        float p = progress_.current() + dt / hold_;
        if (p >= 1)
        {
            p = 1;
            fired_ = true;
            fire();
        }
        progress_.jump(p);
        ui::invalidate();
    }
    switch (state_)
    {
    case Idle:
        if (loading())
        {
            state_ = Loading;
            label_->offsetY()->set(-rect().h);
        }
        break;
    case Succeeded:
    case Failed:
        timer_ += dt;
        if (timer_ >= kResultHold)
        {
            state_ = Idle;
            result_.set(2);
            label_->offsetY()->jump(rect().h);
            label_->offsetY()->set(0);
            if (!holding_)
                progress_.set(0);
        }
        break;
    default:
        break;
    }
}

// cursorShape 随着按住进度从左把光标吃掉，于是它在右边缘耗尽。
Rect Button::cursorShape(Rect r)
{
    float cut = r.w * clampf(progress_.get(), 0, 1);
    r.x += cut;
    r.w -= cut;
    return r;
}

void Button::draw(gfx::Canvas &c)
{
    Rect r = rect();
    int16_t cx = ui::dp(r.w / 2), cy = ui::dp(r.h / 2);
    int16_t d3 = ui::dp(3), d4 = ui::dp(4);
    gfx::Color col = ui::theme.foreground;
    if (state_ == Loading)
        drawDots(c, cx, cy, elapsed_, col);
    if (mark_ != Idle && result_.get() < 1.99f)
    {
        int16_t y = int16_t(cy + ui::dp((1 - result_.get()) * r.h));
        if (mark_ == Succeeded)
        {
            int16_t d1 = ui::dp(1);
            c.line(int16_t(cx - d4), y, int16_t(cx - d1), int16_t(y + d3), col);
            c.line(int16_t(cx - d1), int16_t(y + d3), int16_t(cx + d4), int16_t(y - d3), col);
        }
        else
        {
            c.line(int16_t(cx - d3), int16_t(y - d3), int16_t(cx + d3), int16_t(y + d3), col);
            c.line(int16_t(cx - d3), int16_t(y + d3), int16_t(cx + d3), int16_t(y - d3), col);
        }
    }
}

// ----------------------------------------------------------------- Mark

Mark::Mark(bool radio) : radio_(radio)
{
    on_.setSpring(&anim::springBouncy);
    on_.setPrecision(0.01f);
}

Vec2 Mark::measure(Vec2 max)
{
    (void)max;
    return Vec2{kMarkSize, kMarkSize};
}

void Mark::turn(bool on)
{
    on_.set(on ? 1.0f : 0.0f);
    ui::invalidate();
}

void Mark::jump(bool on)
{
    on_.jump(on ? 1.0f : 0.0f);
    ui::invalidate();
}

void Mark::draw(gfx::Canvas &c)
{
    gfx::Color col = ui::theme.foreground;
    int16_t size = rect().pixels().w;
    int16_t h = int16_t(size / 2);
    int16_t fill = ui::round16(clampf(on_.get(), 0, 1.3f) * float(size) * 0.29f);
    if (radio_)
    {
        c.strokeCircle(h, h, h, col);
        if (fill > 0)
            c.fillCircle(h, h, int16_t(fill - 1), col);
        return;
    }
    c.strokeRoundRect(gfx::Rect{0, 0, size, size}, ui::dp(2), col);
    if (fill > 0)
    {
        c.fillRect(
            gfx::Rect{int16_t(h - fill + 1), int16_t(h - fill + 1), int16_t(2 * fill - 1), int16_t(2 * fill - 1)}, col);
    }
}

// ------------------------------------------------------------- CheckBox

CheckBox::CheckBox(const char *title, bool checked, std::initializer_list<Option> opts) : ListItem(title)
{
    checked_ = checked;
    mark_ = new Mark(false);
    if (checked)
        mark_->jump(true);
    setTrailing(mark_);
    activate = [this]() { toggle(); };
    ui::apply(this, opts);
}

void CheckBox::setChecked(bool v)
{
    checked_ = v;
    mark_->turn(v);
}

void CheckBox::toggle()
{
    bool from = checked_;
    setChecked(!from);
    Action call = chain(changeBoolAction(onCheck, from, !from), onClick);
    run(call, [this, from](bool ok) {
        if (!ok)
        {
            setChecked(from);
            shake(this);
        }
    });
}

// ------------------------------------------------------------ RadioGroup

void RadioGroup::select(int i)
{
    selected_ = i;
    for (int j = 0; j < count_; j++)
    {
        if (radios_[j])
            radios_[j]->mark_->turn(j == i);
    }
}

Radio::Radio(RadioGroup *group, const char *title, std::initializer_list<Option> opts) : ListItem(title)
{
    group_ = group;
    index_ = group ? group->count_ : 0;
    if (group && group->count_ < RadioGroup::kMaxRadios)
        group->radios_[group->count_++] = this;
    mark_ = new Mark(true);
    if (group && index_ == group->selected())
        mark_->jump(true);
    setTrailing(mark_);
    activate = [this]() { choose(); };
    ui::apply(this, opts);
}

// choose 在组处理器运行期间显示自己的等待，并挡住组里其他行。
void Radio::choose()
{
    RadioGroup *g = group_;
    if (!g || g->busy_)
        return;
    int from = g->selected(), to = index_;
    g->select(to);
    if (g->picked)
        g->picked(from, to);
    Action call = chain(changeIntAction(g->onCheck, from, to), onClick);
    g->busy_ = call ? true : false;
    run(call, [this, g, from](bool ok) {
        g->busy_ = false;
        if (!ok)
        {
            g->select(from);
            shake(this);
        }
    });
}

// ---------------------------------------------------------------- Select

Select::Select(const char *title, const char *const *options, int count, int selected,
               std::initializer_list<Option> opts)
    : ListItem(title)
{
    options_ = options;
    count_ = count;
    selected_ = selected;
    value_ = new Label(optionText());
    value_->setFont(ui::theme.smallFont);
    setTrailing(value_);
    activate = [this]() { open(); };
    ui::apply(this, opts);
}

const char *Select::optionText() const
{
    if (!options_ || selected_ < 0 || selected_ >= count_)
        return "";
    return options_[selected_];
}

void Select::setSelected(int i)
{
    selected_ = i;
    value_->setText(optionText());
}

// GroupHolder 替 Select 拿着临时建的 RadioGroup，抽屉销毁时一并释放。
namespace
{

struct GroupHolder : ui::Node
{
    explicit GroupHolder(RadioGroup *g) : group(g)
    {
        setVisible(false);
    }
    ~GroupHolder() override
    {
        delete group;
    }
    RadioGroup *group;
};

} // namespace

void Select::open()
{
    Drawer *d = new Drawer(Bottom, title_->text());
    List *list = new List();
    RadioGroup *group = new RadioGroup(selected_);
    group->picked = [this, d](int from, int to) {
        d->close();
        setSelected(to);
        Action call = chain(changeIntAction(onChange, from, to), onClick);
        run(call, [this, from](bool ok) {
            if (!ok)
            {
                setSelected(from);
                shake(this);
            }
        });
    };
    for (int i = 0; i < count_; i++)
        ui::add(list, new Radio(group, options_[i]));
    // 列表按行数定高，短列表就不会撑满抽屉。
    float row = ui::lineHeight(ui::theme.font) + ui::theme.rowPadding.vertical();
    float room = ui::screenHeight() * 0.8f - d->titleHeight() - ui::theme.panelPadding.t;
    float h = row * float(count_);
    if (h > room)
        h = room;
    list->setHeight(ui::px(h));
    ui::add(d, list);
    ui::attach(d, new GroupHolder(group));
    d->open();
    if (group->selected() >= 0 && group->selected() < group->count())
        ui::setFocus(group->radioAt(group->selected()));
}

// ---------------------------------------------------------------- Drawer

Drawer::Drawer(Edge edge, const char *title, std::initializer_list<Option> opts) : edge_(edge)
{
    setAxis(ui::Vertical);
    if (title && title[0])
    {
        const ui::Theme &t = ui::theme;
        title_ = new Label(title);
        title_->setFont(t.smallFont);
        title_->setColor(t.muted);
        title_->setPadding(t.rowPadding);
        ui::attach(this, title_);
    }
    ui::apply(this, opts);
}

void Drawer::open()
{
    ui::open(this, ui::Popup(int(ui::PopupDrawerBottom) + int(edge_)));
}

void Drawer::close()
{
    ui::close(this);
}

float Drawer::titleHeight() const
{
    if (!title_)
        return 0;
    return ui::lineHeight(title_->font()) + title_->padding().vertical();
}

// ---------------------------------------------------------------- Dialog

Dialog::Dialog(const char *title, const char *message, std::initializer_list<Button *> buttons)
{
    const ui::Theme &t = ui::theme;
    setAxis(ui::Vertical);
    setGap(t.gap);
    setWidth(ui::rel(1));
    title_ = new Label(title);
    title_->setAlign(0.5f);
    actions_ = new Flex(ui::Horizontal);
    actions_->setGap(t.gap);
    for (Button *b : buttons)
    {
        b->setGrow(1);
        b->after = [this](bool ok) {
            if (ok)
                close();
        };
        ui::attach(actions_, b);
    }
    ui::attach(this, title_);
    if (message && message[0])
    {
        message_ = new Label(message);
        message_->setFont(t.smallFont);
        message_->setAlign(0.5f);
        message_->setMarquee(MarqueeAlways);
        ui::attach(this, message_);
    }
    ui::attach(this, actions_);
}

void Dialog::open()
{
    ui::open(this, ui::PopupDialog);
}

void Dialog::close()
{
    ui::close(this);
}

// ---------------------------------------------------------------- Toast

namespace
{

// kToastHold 是 toast 停留的时间。
constexpr float kToastHold = 2;

// ToastLabel 到点自己关掉。
class ToastLabel : public Label
{
  public:
    explicit ToastLabel(const char *text) : Label(text)
    {
    }
    ~ToastLabel();

    void update(float dt) override;

  private:
    float timer_ = 0;
};

ToastLabel *gLastToast = nullptr;

// 层栈回收这一层时会连内容一起删掉，用 Back 关掉的 toast 不经过 update，
// 所以要在析构里把记住的那一格擦掉，免得下次 msg 打到已经释放的对象上。
ToastLabel::~ToastLabel()
{
    if (gLastToast == this)
        gLastToast = nullptr;
}

} // namespace

void ToastLabel::update(float dt)
{
    Label::update(dt);
    timer_ += dt;
    if (timer_ >= kToastHold && timer_ - dt < kToastHold)
        ui::close(this);
}

void toast(const char *text)
{
    // 新 toast 顶掉上一条，免得两条叠在同一处看不清。
    if (gLastToast)
        ui::close(gLastToast);
    ToastLabel *t = new ToastLabel(text);
    t->setFont(ui::theme.smallFont);
    t->setMarquee(MarqueeAlways);
    t->setPadding(ui::theme.rowPadding);
    gLastToast = t;
    ui::open(t, ui::PopupToast);
}

} // namespace widget
} // namespace lilty
