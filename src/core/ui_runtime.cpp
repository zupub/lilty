// ui 的动态部分：焦点查找、层栈、输入分发、光标、帧循环。
#include "core/ui.h"

namespace lilty
{
namespace ui
{

// 共享状态定义在 ui_core.cpp。
extern Node *gRoot;
extern gfx::Screen *gScreen;
extern int16_t gPhysW;
extern int16_t gPhysH;
extern float gScrW;
extern float gScrH;
extern bool gNeedsLayout;
extern bool gNeedsDraw;
extern float gScale;

static Node *gFocus = nullptr;

// ---------------------------------------------------------------- 几何小工具

// shown 报告 d 和它的所有祖先是否都可见。
static bool shown(Node *d)
{
    for (Node *n = d; n; n = n->parent())
    {
        if (!n->visible())
            return false;
    }
    return true;
}

// restOrigin 把布局位置和偏移目标累加起来，于是焦点判断用的是节点将会在的
// 位置，而不是动画此刻把它放在的位置。
static void restOrigin(Node *d, float &x, float &y)
{
    x = y = 0;
    for (Node *n = d->parent(); n; n = n->parent())
    {
        x += n->rect().x + n->offsetX()->target();
        y += n->rect().y + n->offsetY()->target();
    }
}

Rect restRect(Node *d)
{
    float ox, oy;
    restOrigin(d, ox, oy);
    return Rect{ox + d->rect().x + d->offsetX()->target(), oy + d->rect().y + d->offsetY()->target(), d->rect().w,
                d->rect().h};
}

static gfx::Rect pixelRect(Node *b)
{
    Rect r{b->rect().x + b->offsetX()->get(), b->rect().y + b->offsetY()->get(), b->rect().w, b->rect().h};
    return r.pixels();
}

// pixelOrigin 是 d 左上角在物理像素下的位置，和 paint 算的一致。
static void pixelOrigin(Node *d, int16_t &x, int16_t &y)
{
    x = y = 0;
    for (Node *n = d; n; n = n->parent())
    {
        gfx::Rect p = pixelRect(n);
        x = int16_t(x + p.x);
        y = int16_t(y + p.y);
    }
}

static float absf(float v)
{
    return v < 0 ? -v : v;
}

// ---------------------------------------------------------------- 光标

static Rect screenRect(Node *d);

// Cursor 是标记焦点的唯一高光。四条边各自运动，于是高光朝焦点去的地方拉长
// 再收拢。它活在屏幕空间里：焦点换屏时它不会跟着旧屏消失，而是跳过去。
struct Cursor
{
    anim::Value l, t, r, b;
    anim::Value press;
    anim::Value lift;
    anim::Value shiftX, shiftY;
    float leanX = 0, leanY = 0;

    Rect target{0, 0, 0, 0};
    bool has = false;

    void init()
    {
        press.setPrecision(0.01f);
        lift.setSpring(&cursorLeap);
        shiftX.setSpring(&cursorLean);
        shiftY.setSpring(&cursorLean);
    }

    // pressed 快速收缩且不带弹跳，把回弹留给松手。
    void pressed(Phase p)
    {
        if (p == Press)
        {
            press.setSpring(&anim::springInteractive);
            press.set(1);
        }
        else if (p == Release)
        {
            press.setSpring(&cursorRelease);
            press.set(0);
        }
    }

    // lean 把光标挪 dx, dy 并停在那里：按键按下时的预告，松手才落实移动。
    void lean(float dx, float dy)
    {
        leanX = dx;
        leanY = dy;
    }

    // leap 让光标跳一下：抬起、变大，落地时压扁。换屏时调用。
    void leap()
    {
        lift.bump(3);
    }

    // nudge 把光标往侧面甩 px，再让它弹回来。
    void nudge(float px)
    {
        shiftX.bump(px);
    }

    void clear()
    {
        anim::Value *all[] = {&l, &t, &r, &b, &press, &lift, &shiftX, &shiftY};
        for (anim::Value *v : all)
            v->jump(0);
        target = Rect{0, 0, 0, 0};
        has = false;
        leanX = leanY = 0;
    }

    void moveTo(Rect nr)
    {
        Vec2 oldC = target.center(), newC = nr.center();
        edgeSpring(&l, &r, newC.x - oldC.x);
        edgeSpring(&t, &b, newC.y - oldC.y);
        target = nr;
        l.set(nr.x);
        t.set(nr.y);
        r.set(nr.right());
        b.set(nr.bottom());
    }

    // edgeSpring 让朝运动方向的那条边用更快的弹簧。
    void edgeSpring(anim::Value *lo, anim::Value *hi, float d)
    {
        if (d > 0)
        {
            lo->setSpring(&cursorTrail);
            hi->setSpring(&cursorLead);
        }
        else if (d < 0)
        {
            lo->setSpring(&cursorLead);
            hi->setSpring(&cursorTrail);
        }
        else
        {
            lo->setSpring(&cursorLead);
            hi->setSpring(&cursorLead);
        }
    }

    void update();
    void draw(gfx::Canvas &c);
};

static Cursor cursor;

// --------------------------------------------------------------- 焦点管理

static void revealAll(Node *d)
{
    for (Node *n = d->parent(); n; n = n->parent())
        n->reveal(d);
}

void setFocus(Node *d)
{
    if (d && (!d->focusable() || !shown(d)))
        return;
    if (gFocus == d)
        return;
    Node *old = gFocus;
    gFocus = d;
    if (old)
        old->focusChanged(false);
    if (d)
    {
        d->focusChanged(true);
        revealAll(d);
    }
    invalidate();
}

Node *focused()
{
    return gFocus;
}

static void centerDist(const Rect &a, const Rect &b, float &out)
{
    Vec2 ac = a.center(), bc = b.center();
    float dx = ac.x - bc.x, dy = ac.y - bc.y;
    out = dx * dx + dy * dy;
}

// Finder 按 Android 的 FocusFinder 找下一个焦点：先看方向光柱里的候选，
// 再比 13·主轴² + 次轴² 的距离。
struct Finder
{
    Key dir = Key::None;
    Rect src{0, 0, 0, 0};
    Node *from = nullptr;
    Node *exclude = nullptr;
    Node *best = nullptr;
    Rect bestRect{0, 0, 0, 0};

    void walk(Node *scope)
    {
        float x, y;
        restOrigin(scope, x, y);
        visit(scope, x, y);
    }

    void visit(Node *d, float ox, float oy)
    {
        if (!d->visible() || d == exclude)
            return;
        float x = ox + d->rect().x + d->offsetX()->target();
        float y = oy + d->rect().y + d->offsetY()->target();
        if (d->focusable() && d != from)
        {
            Rect r{x, y, d->rect().w, d->rect().h};
            if ((best == nullptr && candidate(r)) || (best != nullptr && better(r, bestRect)))
            {
                best = d;
                bestRect = r;
            }
        }
        for (Node *c = d->firstChild(); c; c = c->nextSibling())
            visit(c, x, y);
    }

    bool candidate(const Rect &r) const
    {
        const Rect &s = src;
        switch (dir)
        {
        case Key::Right:
            return (s.x < r.x || s.right() <= r.x) && s.right() < r.right();
        case Key::Left:
            return (s.right() > r.right() || s.x >= r.right()) && s.x > r.x;
        case Key::Down:
            return (s.y < r.y || s.bottom() <= r.y) && s.bottom() < r.bottom();
        case Key::Up:
            return (s.bottom() > r.bottom() || s.y >= r.bottom()) && s.y > r.y;
        default:
            return true;
        }
    }

    bool inBeam(const Rect &r) const
    {
        const Rect &s = src;
        if (dir == Key::Left || dir == Key::Right)
            return r.bottom() > s.y && r.y < s.bottom();
        return r.right() > s.x && r.x < s.right();
    }

    // beyond 报告 r 是否完全在源点沿该方向的后面。
    bool beyond(const Rect &r) const
    {
        const Rect &s = src;
        switch (dir)
        {
        case Key::Right:
            return s.right() <= r.x;
        case Key::Left:
            return s.x >= r.right();
        case Key::Down:
            return s.bottom() <= r.y;
        default:
            return s.y >= r.bottom();
        }
    }

    float major(const Rect &r) const
    {
        const Rect &s = src;
        float d = 0;
        switch (dir)
        {
        case Key::Right:
            d = r.x - s.right();
            break;
        case Key::Left:
            d = s.x - r.right();
            break;
        case Key::Down:
            d = r.y - s.bottom();
            break;
        case Key::Up:
        default:
            d = s.y - r.bottom();
            break;
        }
        return d > 0 ? d : 0;
    }

    float majorFar(const Rect &r) const
    {
        const Rect &s = src;
        float d = 0;
        switch (dir)
        {
        case Key::Right:
            d = r.right() - s.right();
            break;
        case Key::Left:
            d = s.x - r.x;
            break;
        case Key::Down:
            d = r.bottom() - s.bottom();
            break;
        case Key::Up:
        default:
            d = s.y - r.y;
            break;
        }
        return d > 1 ? d : 1;
    }

    bool beamBeats(const Rect &a, const Rect &b) const
    {
        if (!inBeam(a) || inBeam(b))
            return false;
        if (!beyond(b) || dir == Key::Left || dir == Key::Right)
            return true;
        return major(a) < majorFar(b);
    }

    float weight(const Rect &r) const
    {
        float m = major(r);
        Vec2 sc = src.center(), rc = r.center();
        float minor = absf(sc.x - rc.x);
        if (dir == Key::Left || dir == Key::Right)
            minor = absf(sc.y - rc.y);
        return 13 * m * m + minor * minor;
    }

    bool better(const Rect &a, const Rect &b) const
    {
        if (!candidate(a))
            return false;
        if (dir == Key::None)
        {
            float da, db;
            centerDist(src, a, da);
            centerDist(src, b, db);
            return da < db;
        }
        if (beamBeats(a, b))
            return true;
        if (beamBeats(b, a))
            return false;
        return weight(a) < weight(b);
    }
};

Node *firstFocusable(Node *d)
{
    if (!d || !d->visible())
        return nullptr;
    if (d->focusable())
        return d;
    for (Node *c = d->firstChild(); c; c = c->nextSibling())
    {
        Node *f = firstFocusable(c);
        if (f)
            return f;
    }
    return nullptr;
}

Node *findFocus(Node *scope, Node *from, Key dir)
{
    if (!from)
        return firstFocusable(scope);
    Finder f;
    f.dir = dir;
    f.src = restRect(from);
    f.from = from;
    f.walk(scope);
    return f.best;
}

// ---------------------------------------------------------------- 层栈

struct Layer;
struct LayerStack;

static Node *layersScope();

// focusLeaving 在 b 被移除或隐藏之前把焦点挪到最近的节点上。
void focusLeaving(Node *b)
{
    if (!gFocus || !isAncestor(b, gFocus))
        return;
    Node *next = nullptr;
    Node *scope = layersScope();
    if (scope)
    {
        Finder fd;
        fd.dir = Key::None;
        fd.src = restRect(gFocus);
        fd.from = gFocus;
        fd.exclude = b;
        fd.walk(scope);
        next = fd.best;
    }
    setFocus(nullptr);
    setFocus(next);
}

// layer 包住每一页或弹窗，让转场把它当成一个整体移动。
struct Layer : public Node
{
    Node *content = nullptr;
    Popup kind = PopupFullPage;
    // frame 是弹窗的外框，包住内容和内边距。
    Rect frame{0, 0, 0, 0};
    // focusRestore 是上层关闭后要恢复的焦点。
    Node *focusRestore = nullptr;
    // placed 表示入场动画已经开始，需要已布局的内容。
    bool placed = false;
    bool exiting = false;

    bool modal() const
    {
        return kind != PopupToast;
    }
    bool isLayouter() const override
    {
        return true;
    }
    Vec2 measureChildren(Vec2 max) override
    {
        return max;
    }
    void arrangeChildren(Rect r) override;
    void draw(gfx::Canvas &c) override;

    Insets inset() const;
    Vec2 fit(Vec2 room) const;
    Rect place(Rect r) const;
    void away(float &dx, float &dy) const;
    void peek(float d, float &dx, float &dy);
    bool settled() const
    {
        return offsetX()->settled() && offsetY()->settled();
    }
};

// inset 是弹窗外框与内容之间的余量。抽屉贴边那条边留 0。
Insets Layer::inset() const
{
    Insets p = theme.panelPadding;
    switch (kind)
    {
    case PopupDrawerBottom:
        p.b = 0;
        break;
    case PopupDrawerTop:
        p.t = 0;
        break;
    case PopupDrawerLeft:
        p.l = 0;
        break;
    case PopupDrawerRight:
        p.r = 0;
        break;
    default:
        break;
    }
    return p;
}

// fit 是弹窗在 room 里需要的框尺寸：内容加上内边距。
Vec2 Layer::fit(Vec2 room) const
{
    Insets p = inset();
    Vec2 inner{room.x - p.horizontal(), room.y - p.vertical()};
    if (inner.x < 0)
        inner.x = 0;
    if (inner.y < 0)
        inner.y = 0;
    Vec2 s = measureNode(content, inner);
    float w = s.x + p.horizontal();
    float h = s.y + p.vertical();
    return Vec2{w < room.x ? w : room.x, h < room.y ? h : room.y};
}

// place 定出弹窗外框的大小，并钉到对应的边或正中。
Rect Layer::place(Rect r) const
{
    switch (kind)
    {
    case PopupDrawerBottom:
    case PopupDrawerTop: {
        float h = fit(Vec2{r.w, r.h}).y;
        if (kind == PopupDrawerTop)
            return Rect{r.x, r.y, r.w, h};
        return Rect{r.x, r.bottom() - h, r.w, h};
    }
    case PopupDrawerLeft:
    case PopupDrawerRight: {
        float w = fit(Vec2{r.w, r.h}).x;
        if (kind == PopupDrawerLeft)
            return Rect{r.x, r.y, w, r.h};
        return Rect{r.right() - w, r.y, w, r.h};
    }
    default:
        break;
    }
    const float m = 4;
    Vec2 s = fit(Vec2{r.w - 2 * m, r.h - 2 * m});
    float x = r.x + (r.w - s.x) / 2;
    if (kind == PopupToast)
        return Rect{x, r.bottom() - m - s.y, s.x, s.y};
    return Rect{x, r.y + (r.h - s.y) / 2, s.x, s.y};
}

void Layer::arrangeChildren(Rect r)
{
    if (kind == PopupFullPage)
    {
        arrange(content, r);
        return;
    }
    frame = place(r);
    Insets p = inset();
    Rect f = frame;
    arrange(content, Rect{f.x + p.l, f.y + p.t, f.w - p.horizontal(), f.h - p.vertical()});
    if (!placed)
    {
        placed = true;
        float dx, dy;
        away(dx, dy);
        offsetX()->jump(dx);
        offsetY()->jump(dy);
        offsetX()->set(0);
        offsetY()->set(0);
    }
}

// away 是把层送到屏幕外的偏移：它就是从那个方向进来的，也从那里离开。
void Layer::away(float &dx, float &dy) const
{
    const Rect &c = frame;
    dx = dy = 0;
    switch (kind)
    {
    case PopupFullPage:
        dx = gScrW;
        return;
    case PopupDrawerBottom:
    case PopupToast:
        dy = gScrH - c.y + 1;
        return;
    case PopupDrawerTop:
        dy = -c.bottom() - 1;
        return;
    case PopupDrawerLeft:
        dx = -c.right() - 1;
        return;
    case PopupDrawerRight:
        dx = gScrW - c.x + 1;
        return;
    default:
        dy = -c.bottom() - 1;
        return;
    }
}

// peek 把层往它离开的方向推 d，返回实际位移。
void Layer::peek(float d, float &dx, float &dy)
{
    float ax, ay;
    away(ax, ay);
    dx = ax > 0 ? d : (ax < 0 ? -d : 0);
    dy = ay > 0 ? d : (ay < 0 ? -d : 0);
    offsetX()->set(dx);
    offsetY()->set(dy);
}

void Layer::draw(gfx::Canvas &c)
{
    const Theme &t = theme;
    if (kind == PopupFullPage)
    {
        c.fillRect(Rect{0, 0, rect().w, rect().h}.pixels(), t.background);
        return;
    }
    gfx::Rect r = frame.pixels();
    int16_t rad = dp(t.radius);
    if (kind <= PopupDrawerRight)
    {
        // 抽屉的外框跑出屏幕，所以只露出内侧的圆角。
        rad = int16_t(rad * 2);
        int16_t ext = int16_t(rad + 1);
        switch (kind)
        {
        case PopupDrawerBottom:
            r.h = int16_t(r.h + ext);
            break;
        case PopupDrawerTop:
            r.y = int16_t(r.y - ext);
            r.h = int16_t(r.h + ext);
            break;
        case PopupDrawerLeft:
            r.x = int16_t(r.x - ext);
            r.w = int16_t(r.w + ext);
            break;
        case PopupDrawerRight:
            r.w = int16_t(r.w + ext);
            break;
        default:
            break;
        }
    }
    c.fillRoundRect(r, rad, t.background);
    c.strokeRoundRect(r, rad, t.foreground);
}

// LayerStack 按绘制顺序放着页面和弹窗。只有最上面的模态层接收焦点和输入；
// 被其他页面完全盖住的页面会被隐藏。
struct LayerStack
{
    static constexpr int kMaxLayers = 8;
    Layer *list[kMaxLayers];
    int count = 0;
    bool focusPending = false;

    LayerStack() : list{}
    {
    }

    void add(Node *n, Popup kind);
    bool close(Layer *l);
    Layer *find(Node *n);
    Layer *top();
    Layer *pageBelow(Layer *l);
    void back(KeyEvent &e);
    Node *scope();
    void update();
    void focusFirst();
    void removeAll();
};

static LayerStack layers;

static Node *layersScope()
{
    return layers.scope();
}

void LayerStack::add(Node *n, Popup kind)
{
    if (count >= kMaxLayers)
        return;
    Layer *l = new Layer();
    l->content = n;
    l->kind = kind;
    l->setWidth(rel(1));
    l->setHeight(rel(1));
    l->offsetX()->setSpring(&anim::springSmooth);
    l->offsetY()->setSpring(&anim::springSmooth);
    if (kind == PopupDialog)
        l->offsetY()->setSpring(&anim::springDefault);
    attach(l, n);
    if (kind == PopupFullPage)
    {
        l->placed = true;
        if (Layer *p = pageBelow(nullptr))
        {
            p->offsetX()->set(-gScrW / 3);
            l->offsetX()->jump(gScrW);
            l->offsetX()->set(0);
        }
    }
    if (kind != PopupToast)
    {
        if (Layer *t = top())
        {
            t->focusRestore = gFocus;
            cursor.leap();
        }
        setFocus(nullptr);
        focusPending = true;
    }
    list[count++] = l;
    attach(gRoot, l);
}

bool LayerStack::close(Layer *l)
{
    if (!l || l->exiting)
        return false;
    Layer *below = pageBelow(l);
    if (l->kind == PopupFullPage && !below)
        return false;
    bool wasTop = l == top();
    l->exiting = true;
    float dx, dy;
    l->away(dx, dy);
    l->offsetX()->set(dx);
    l->offsetY()->set(dy);
    if (l->kind == PopupFullPage && below)
    {
        below->offsetX()->set(0);
        below->setVisible(true);
    }
    if (wasTop)
    {
        cursor.leap();
        setFocus(nullptr);
        if (Layer *t = top())
        {
            t->setVisible(true);
            setFocus(t->focusRestore);
        }
        focusPending = true;
    }
    return true;
}

Layer *LayerStack::find(Node *n)
{
    for (int i = count - 1; i >= 0; i--)
    {
        Layer *l = list[i];
        if (!l->exiting && l->content == n)
            return l;
    }
    return nullptr;
}

Layer *LayerStack::top()
{
    for (int i = count - 1; i >= 0; i--)
    {
        Layer *l = list[i];
        if (!l->exiting && l->modal())
            return l;
    }
    return nullptr;
}

Layer *LayerStack::pageBelow(Layer *l)
{
    bool found = l == nullptr;
    for (int i = count - 1; i >= 0; i--)
    {
        Layer *c = list[i];
        if (c == l)
        {
            found = true;
        }
        else if (found && !c->exiting && c->kind == PopupFullPage)
        {
            return c;
        }
    }
    return nullptr;
}

// back 处理没人认领的返回键：按下时把最上层朝它离开的方向探出一点，松手
// 关掉它；根页面只轻轻顶一下。
void LayerStack::back(KeyEvent &e)
{
    Layer *t = top();
    if (!t)
        return;
    float d = 8;
    if (t->kind == PopupFullPage && pageBelow(t) == nullptr)
        d /= 2;
    if (e.phase == Press)
    {
        float dx, dy;
        t->peek(d, dx, dy);
        cursor.lean(dx, dy);
    }
    else if (e.phase == Release)
    {
        cursor.lean(0, 0);
        float dx, dy;
        t->peek(0, dx, dy);
        if (e.canceled || close(t))
            return;
        t->offsetX()->bump(6);
        cursor.nudge(6);
    }
}

Node *LayerStack::scope()
{
    Layer *t = top();
    return t ? static_cast<Node *>(t) : nullptr;
}

void LayerStack::update()
{
    for (int i = 0; i < count;)
    {
        Layer *l = list[i];
        if (l->exiting && l->settled())
        {
            for (int k = i; k < count - 1; k++)
                list[k] = list[k + 1];
            list[--count] = nullptr;
            remove(l);
            delete l;
            continue;
        }
        i++;
    }
    // 被 Back 推到一边的页面只盖住一部分宽度，所以页面会一直叠到它们的静止
    // 区间合起来覆盖整屏为止。
    int cover = -1;
    float lo = 0, hi = 0;
    bool have = false;
    for (int i = count - 1; i >= 0; i--)
    {
        Layer *l = list[i];
        if (l->exiting || l->kind != PopupFullPage || !l->settled() || l->offsetY()->target() != 0)
            continue;
        float x = l->offsetX()->target();
        if (!have)
        {
            lo = x;
            hi = x + gScrW;
            have = true;
        }
        else if (x <= hi && x + gScrW >= lo)
        {
            if (x < lo)
                lo = x;
            if (x + gScrW > hi)
                hi = x + gScrW;
        }
        if (lo <= 0 && hi >= gScrW)
        {
            cover = i;
            break;
        }
    }
    for (int i = 0; i < count; i++)
        list[i]->setVisible(i >= cover);
}

// focusFirst 在新层第一次布局之后跑，这样把焦点滚进视野时拿到的是真实矩形。
void LayerStack::focusFirst()
{
    if (!focusPending)
        return;
    focusPending = false;
    if (gFocus)
    {
        revealAll(gFocus);
        return;
    }
    if (Node *sc = scope())
        setFocus(firstFocusable(sc));
}

void LayerStack::removeAll()
{
    while (count > 0)
    {
        Layer *l = list[--count];
        remove(l);
        delete l;
    }
}

void push(Node *p)
{
    layers.add(p, PopupFullPage);
}
bool pop()
{
    return layers.close(layers.top());
}
void open(Node *n, Popup popup)
{
    layers.add(n, popup);
}
bool close(Node *n)
{
    return layers.close(layers.find(n));
}

// ---------------------------------------------------------------- 光标实现

// screenRect 返回 d 在逻辑屏幕坐标下的矩形，和 paint 摆放它的方式一致，
// 并被裁剪祖先裁过。完全不可见时返回未裁剪的矩形，免得光标飞到角上。
static Rect screenRect(Node *d)
{
    int16_t x, y;
    pixelOrigin(d, x, y);
    gfx::Rect p = pixelRect(d);
    gfx::Rect own{x, y, p.w, p.h};
    gfx::Rect vis = own.intersect(gfx::Rect{0, 0, gPhysW, gPhysH});
    for (Node *n = d->parent(); n; n = n->parent())
    {
        if (!n->clipEnabled())
            continue;
        int16_t cx, cy;
        pixelOrigin(n, cx, cy);
        gfx::Rect c = pixelRect(n);
        vis = vis.intersect(gfx::Rect{cx, cy, c.w, c.h});
    }
    if (vis.empty())
        vis = own;
    float k = 1.0f / gScale;
    return Rect{float(vis.x) * k, float(vis.y) * k, float(vis.w) * k, float(vis.h) * k};
}

void Cursor::update()
{
    Node *f = gFocus;
    if (!f || !shown(f))
    {
        if (has)
        {
            has = false;
            Vec2 m = target.center();
            moveTo(Rect{m.x, m.y, 0, 0});
        }
        shiftX.set(0);
        shiftY.set(0);
        return;
    }
    Rect r = screenRect(f);
    r = f->cursorShape(r);
    if (!has)
    {
        // 从目标中心长出来，而不是从上一个位置飞过来。
        Vec2 m = r.center();
        l.jump(m.x);
        this->r.jump(m.x);
        t.jump(m.y);
        b.jump(m.y);
        has = true;
        invalidate();
    }
    if (r.x != target.x || r.y != target.y || r.w != target.w || r.h != target.h)
        moveTo(r);
    shiftX.set(leanX);
    shiftY.set(leanY);
}

void Cursor::draw(gfx::Canvas &c)
{
    float liftV = lift.get();
    float in = press.get() * 3 - liftV * 2 / 3;
    float dx = shiftX.get(), dy = shiftY.get() - liftV;
    Rect rr{l.get() + in + dx, t.get() + in / 2 + dy, 0, 0};
    rr.w = this->r.get() - in + dx - rr.x;
    rr.h = b.get() - in / 2 + dy - rr.y;
    if (rr.w < 1 || rr.h < 1)
        return;
    gfx::Rect p = rr.pixels();
    int16_t rad = dp(theme.radius);
    if (p.h / 2 < rad)
        rad = int16_t(p.h / 2);
    if (p.w / 2 < rad)
        rad = int16_t(p.w / 2);
    if (c.canInvert())
        c.invertRoundRect(p, rad);
    else
        c.strokeRoundRect(p, rad, theme.accent);
}

// -------------------------------------------------------------------- 输入

// leanDist 是按住方向键时光标提前探出的距离。
static constexpr float kLeanDist = 2;

static Node *gOwner[kKeyCount];
static bool gNav[kKeyCount];
static bool gRepeated[kKeyCount];
static Node *gPath[64];

static void dispatch(KeyEvent &e);
static void navigate(KeyEvent &e);

static void deliver(Node *n, KeyEvent *e)
{
    if (n->previewKey(*e))
        return;
    n->handleKey(*e);
}

static void claim(KeyEvent &e, Node *n)
{
    if (e.phase == Press)
        gOwner[int(e.key)] = n;
}

static void unclaimed(KeyEvent &e)
{
    if (e.key == Key::Back)
        layers.back(e);
    else
        navigate(e);
}

static void dispatch(KeyEvent &e)
{
    int ki = int(e.key);
    if (ki < 0 || ki >= kKeyCount)
        return;
    if (e.key == Key::OK)
        cursor.pressed(e.phase);
    switch (e.phase)
    {
    case Press:
        gRepeated[ki] = false;
        break;
    case Repeat:
        gRepeated[ki] = true;
        break;
    default:
        e.repeated = gRepeated[ki];
        break;
    }
    if (e.phase != Press)
    {
        if (Node *o = gOwner[ki])
        {
            if (e.phase == Release)
                gOwner[ki] = nullptr;
            deliver(o, &e);
            return;
        }
        if (gNav[ki])
        {
            if (e.phase == Release)
                gNav[ki] = false;
            unclaimed(e);
            return;
        }
        if (e.phase == Release)
            return;
    }
    int len = 0;
    for (Node *n = gFocus; n && len < 64; n = n->parent())
        gPath[len++] = n;
    for (int i = len - 1; i >= 0; i--)
    {
        if (gPath[i]->previewKey(e))
        {
            claim(e, gPath[i]);
            return;
        }
    }
    for (int i = 0; i < len; i++)
    {
        if (gPath[i]->handleKey(e))
        {
            claim(e, gPath[i]);
            return;
        }
    }
    if (e.phase == Press && (isArrow(e.key) || e.key == Key::Back))
    {
        gNav[ki] = true;
        unclaimed(e);
    }
}

static float arrowDx(Key k)
{
    if (k == Key::Left)
        return -1;
    if (k == Key::Right)
        return 1;
    return 0;
}

static float arrowDy(Key k)
{
    if (k == Key::Up)
        return -1;
    if (k == Key::Down)
        return 1;
    return 0;
}

// navigate 用方向键移动焦点。和所有按键一样，按下只给反馈（光标朝按键探
// 出），移动发生在松手时，或者按住期间的每次连发。
//
// 旋钮只给“上一格”和“下一格”，而横排的按钮组要靠左右才能切，所以这里
// 做一次轴向翻译：横排容器里上下当左右用，竖排容器里左右当上下用。翻
// 译前先按原方向找一次，列表这种主轴和旋钮一致的地方就不会被劫持。
static Key crossAxis(Node *cur, Key k)
{
    Node *p = cur ? cur->parent() : nullptr;
    if (!p || p->nodeTag() != kTagFlex)
        return k;
    if (static_cast<Flex *>(p)->axis() == Horizontal)
    {
        if (k == Key::Up)
            return Key::Left;
        if (k == Key::Down)
            return Key::Right;
    }
    else
    {
        if (k == Key::Left)
            return Key::Up;
        if (k == Key::Right)
            return Key::Down;
    }
    return k;
}

static void gatherFocusable(Node *d, Node **out, int &n, int cap)
{
    if (!d || !d->visible() || n >= cap)
        return;
    if (d->focusable())
    {
        out[n++] = d;
        return;
    }
    for (Node *c = d->firstChild(); c; c = c->nextSibling())
        gatherFocusable(c, out, n, cap);
}

// cycleIn 在 box 里按声明顺序取 cur 的前一个 / 后一个可聚焦节点，首尾相
// 接。只有两个按钮的对话框转到头就绕回另一个，不会卡死在一端。
static Node *cycleIn(Node *box, Node *cur, bool forward)
{
    constexpr int kCap = 24;
    Node *arr[kCap];
    int n = 0;
    gatherFocusable(box, arr, n, kCap);
    if (n < 2)
        return nullptr;
    for (int i = 0; i < n; i++)
    {
        if (arr[i] != cur)
            continue;
        return arr[forward ? (i + 1) % n : (i + n - 1) % n];
    }
    return nullptr;
}

static void navigate(KeyEvent &e)
{
    Node *scope = layers.scope();
    if (!scope)
        return;
    Node *cur = gFocus;
    // 光标探向焦点真正要去的那边，而不是按键的字面方向。
    Key dir = cur ? crossAxis(cur, e.key) : e.key;
    if (e.phase == Press)
        cursor.lean(arrowDx(dir) * kLeanDist, arrowDy(dir) * kLeanDist);
    else if (e.phase == Release)
        cursor.lean(0, 0);
    if (!cur || !isAncestor(scope, cur))
    {
        // 没有光标，或者光标还留在下面那层（新弹窗刚盖上来时的常见情况），
        // 都在第一次转动时把它挪进当前层。
        if (e.fires())
            setFocus(firstFocusable(scope));
        return;
    }
    Node *next = findFocus(scope, cur, e.key);
    if (!next && dir != e.key)
    {
        next = findFocus(scope, cur, dir);
        if (!next)
            next = cycleIn(cur->parent(), cur, e.key == Key::Down || e.key == Key::Right);
    }
    for (Node *n = cur->parent(); n; n = n->parent())
    {
        if (n->arrowScroll(e, next))
        {
            if (e.phase == Press)
            {
                // 滚动容器把行拉走了，光标跟着焦点行走，而不是朝反方向探。
                cursor.lean(0, 0);
            }
            return;
        }
    }
    if (next && e.fires())
        setFocus(next);
}

void emit(Key k, Phase p)
{
    if (k == Key::None || k == Key::Count)
        return;
    KeyEvent e(k, p);
    dispatch(e);
}

void emitRune(char r)
{
    KeyEvent e(Key::Char, Press);
    e.rune = r;
    dispatch(e);
}

void doNow(const std::function<void()> &fn)
{
    if (fn)
        fn();
}

void KeyPoller::poll(Key k, bool down, float dt)
{
    int i = int(k);
    if (i <= 0 || i >= kKeyCount)
        return;
    State &s = keys_[i];
    if (down != s.raw)
    {
        s.raw = down;
        s.stable = 0;
    }
    else if (s.stable < debounce)
    {
        s.stable++;
    }
    if (s.stable >= debounce && s.raw != s.down)
    {
        s.down = s.raw;
        if (s.down)
        {
            s.held = 0;
            s.next = repeatDelay;
            s.iv = repeatInterval;
            emit(k, Press);
        }
        else
        {
            emit(k, Release);
        }
        return;
    }
    if (!s.down)
        return;
    s.held += dt;
    if (s.held >= s.next)
    {
        emit(k, Repeat);
        s.next += s.iv;
        s.iv = s.iv * accel > minInterval ? s.iv * accel : minInterval;
    }
}

// -------------------------------------------------------------- 帧与绘制

static void updateTree(Node *d, float dt)
{
    if (!d->visible())
        return;
    d->update(dt);
    for (Node *c = d->firstChild(); c;)
    {
        Node *n = c->nextSibling();
        updateTree(c, dt);
        c = n;
    }
}

static void paint(Node *d, gfx::Canvas c)
{
    if (!d->visible())
        return;
    gfx::Rect p = pixelRect(d);
    c = c.translate(p.x, p.y);
    gfx::Rect local{0, 0, p.w, p.h};
    if (d->clipEnabled())
    {
        if (!c.visible(local))
            return;
        c = c.clip(local);
    }
    if (d->hasBackground() || d->bordered())
    {
        int16_t rad = dp(d->radiusValue());
        if (d->hasBackground())
            c.fillRoundRect(local, rad, d->backgroundColor());
        if (d->bordered())
            c.strokeRoundRect(local, rad, d->borderColor());
    }
    d->draw(c);
    for (Node *ch = d->firstChild(); ch; ch = ch->nextSibling())
        paint(ch, c);
    d->drawOver(c);
}

void boot(gfx::Screen *s, Node *first)
{
    gScreen = s;
    gPhysW = s->width();
    gPhysH = s->height();
    gScale = scaleFor(gPhysW, gPhysH);
    gScrW = float(gPhysW) / gScale;
    gScrH = float(gPhysH) / gScale;
    if (!gRoot)
        gRoot = new Stack();
    gRoot->setWidth(px(gScrW));
    gRoot->setHeight(px(gScrH));
    relayout();
    if (first)
        push(first);
}

void reset()
{
    layers.removeAll();
    gFocus = nullptr;
    cursor.clear();
    for (int i = 0; i < kKeyCount; i++)
    {
        gOwner[i] = nullptr;
        gNav[i] = false;
        gRepeated[i] = false;
    }
}

bool frame(float dt)
{
    // 还没 boot 就跑帧是调用方的 bug，但直接崩在空指针上排查成本很高，
    // 这里直接返回。
    if (!gRoot || !gScreen)
        return false;
    updateTree(gRoot, dt);
    layers.update();
    if (anim::step(dt))
        invalidate();
    if (gNeedsLayout)
    {
        gNeedsLayout = false;
        arrange(gRoot, Rect{0, 0, gScrW, gScrH});
        layers.focusFirst();
    }
    cursor.update();
    if (!gNeedsDraw)
        return false;
    gNeedsDraw = false;

    gfx::Canvas c(gScreen);
    c.fillRect(gfx::Rect{0, 0, gPhysW, gPhysH}, theme.background);
    // Toast 浮在光标之上，这样获得焦点的一行也盖不住它。
    for (int i = 0; i < layers.count; i++)
    {
        if (layers.list[i]->kind != PopupToast)
            paint(layers.list[i], c);
    }
    cursor.draw(c);
    for (int i = 0; i < layers.count; i++)
    {
        if (layers.list[i]->kind == PopupToast)
            paint(layers.list[i], c);
    }
    gScreen->flush();
    return true;
}

} // namespace ui
} // namespace lilty
