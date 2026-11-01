// ui 的静态部分：主题、换算、节点树、布局算法、Flex / Grid / Stack、选项。
#include "core/ui.h"

namespace lilty
{
namespace ui
{

// ------------------------------------------------------------------ 全局状态

Node *gRoot = nullptr;
gfx::Screen *gScreen = nullptr;
int16_t gPhysW = 0;
int16_t gPhysH = 0;
float gScrW = 0;
float gScrH = 0;
bool gNeedsLayout = false;
bool gNeedsDraw = false;

// gScale 由 runtime 部分在 boot 时写入，所以不能是文件内静态的。
float gScale = 1;
static float gZoom = 1;

// 焦点离开某棵子树时由 runtime 部分接管。
void focusLeaving(Node *b);

// ---------------------------------------------------------------------- 主题

// 主题字段按 Theme 的声明顺序一列排开，改颜色时不用数位置。clang-format 会
// 把它们打包成几行交错对齐的样子，所以这一段关掉。
// clang-format off
Theme theme{gfx::Black,
            gfx::White,
            gfx::White,
            gfx::Grey,
            &gfx::font5x7,
            &gfx::font5x7,
            3,
            Insets{4, 1, 4, 1},
            Insets{6, 2, 6, 2},
            2,
            Insets{2, 2, 2, 2}};
// clang-format on

anim::Spring cursorLead{0.22f, 1.0f, true};
anim::Spring cursorTrail{0.52f, 1.0f, true};
anim::Spring cursorRelease{0.57f, 0.40f, false};
anim::Spring cursorLeap{0.63f, 0.45f, false};
anim::Spring cursorLean{0.35f, 0.70f, false};

// ------------------------------------------------------------------ 坐标常量

const Vec2 TopLeft{0, 0};
const Vec2 TopCenter{0.5f, 0};
const Vec2 TopRight{1, 0};
const Vec2 CenterLeft{0, 0.5f};
const Vec2 Center{0.5f, 0.5f};
const Vec2 CenterRight{1, 0.5f};
const Vec2 BottomLeft{0, 1};
const Vec2 BottomCenter{0.5f, 1};
const Vec2 BottomRight{1, 1};

gfx::Rect Rect::pixels() const
{
    int16_t px0 = dp(x), py0 = dp(y);
    return gfx::Rect{px0, py0, int16_t(dp(right()) - px0), int16_t(dp(bottom()) - py0)};
}

// ---------------------------------------------------------------- 缩放与度量

int16_t round16(float x)
{
    if (x < 0)
        return int16_t(-int16_t(-x + 0.5f));
    return int16_t(x + 0.5f);
}

float scale()
{
    return gScale;
}

int16_t dp(float v)
{
    return round16(v * gScale);
}

void setZoom(float f)
{
    if (f > 0)
        gZoom = f;
}

float scaleFor(int16_t w, int16_t h)
{
    float a = float(w) / float(kBaseW);
    float b = float(h) / float(kBaseH);
    float s = (a < b ? a : b) * gZoom;
    return s > 0 ? s : 1;
}

float lineHeight(const gfx::Font *f)
{
    return float(gfx::lineHeight(f)) / gScale;
}

float textWidth(const gfx::Font *f, const char *s)
{
    return float(gfx::textWidth(f, s)) / gScale;
}

float screenWidth()
{
    return gScrW;
}
float screenHeight()
{
    return gScrH;
}

// ------------------------------------------------------------------ 布局调度

void relayout()
{
    gNeedsLayout = true;
    gNeedsDraw = true;
}

void invalidate()
{
    gNeedsDraw = true;
}

// ---------------------------------------------------------------------- Node

Node::~Node()
{
    Node *c = firstChild_;
    while (c)
    {
        Node *n = c->next_;
        c->parent_ = nullptr;
        c->next_ = nullptr;
        c->prev_ = nullptr;
        delete c; // 递归删掉 c 的子孙
        c = n;
    }
}

int Node::childCount() const
{
    int n = 0;
    for (Node *c = firstChild_; c; c = c->next_)
        n++;
    return n;
}

Node *Node::childAt(int i) const
{
    Node *n = firstChild_;
    while (n && i > 0)
    {
        n = n->next_;
        i--;
    }
    return n;
}

void Node::linkChild(Node *child)
{
    child->parent_ = this;
    child->prev_ = lastChild_;
    child->next_ = nullptr;
    if (lastChild_)
        lastChild_->next_ = child;
    else
        firstChild_ = child;
    lastChild_ = child;
}

void Node::unlinkChild(Node *child)
{
    if (child->prev_)
        child->prev_->next_ = child->next_;
    else
        firstChild_ = child->next_;
    if (child->next_)
        child->next_->prev_ = child->prev_;
    else
        lastChild_ = child->prev_;
    child->parent_ = nullptr;
    child->prev_ = nullptr;
    child->next_ = nullptr;
    child->flags_ &= ~kFlagLaidOut;
}

void Node::setWidth(Size s)
{
    if (width_ != s)
    {
        width_ = s;
        relayout();
    }
}

void Node::setHeight(Size s)
{
    if (height_ != s)
    {
        height_ = s;
        relayout();
    }
}

void Node::setAnchor(Vec2 v)
{
    if (anchor_ != v)
    {
        anchor_ = v;
        relayout();
    }
}

void Node::setOrigin(Vec2 v)
{
    if (origin_ != v)
    {
        origin_ = v;
        relayout();
    }
}

void Node::setPosition(Vec2 v)
{
    if (pos_ != v)
    {
        pos_ = v;
        relayout();
    }
}

void Node::setMargin(Insets i)
{
    if (margin_.l != i.l || margin_.t != i.t || margin_.r != i.r || margin_.b != i.b)
    {
        margin_ = i;
        relayout();
    }
}

void Node::setPadding(Insets i)
{
    if (padding_.l != i.l || padding_.t != i.t || padding_.r != i.r || padding_.b != i.b)
    {
        padding_ = i;
        relayout();
    }
}

void Node::setGrow(float g)
{
    if (grow_ != g)
    {
        grow_ = g;
        relayout();
    }
}

void Node::setVisible(bool v)
{
    if (visible() == v)
        return;
    set(kFlagHidden, !v);
    relayout();
    if (!v)
        focusLeaving(this);
}

void Node::setFocusable(bool f)
{
    set(kFlagFocusable, f);
    if (!f)
        focusLeaving(this);
}

void Node::setClip(bool c)
{
    set(kFlagClip, c);
    invalidate();
}

void Node::setBackground(const gfx::Color &c)
{
    bg_ = c;
    set(kFlagBackground, true);
    invalidate();
}

void Node::setBorder(const gfx::Color &c)
{
    border_ = c;
    set(kFlagBorder, true);
    invalidate();
}

void Node::setRadius(float r)
{
    radius_ = r;
    invalidate();
}

void Node::set(uint16_t f, bool on)
{
    if (on)
        flags_ |= f;
    else
        flags_ &= ~f;
}

Rect Node::contentRect() const
{
    const Insets &p = padding_;
    return Rect{p.l, p.t, rect_.w - p.horizontal(), rect_.h - p.vertical()};
}

// ---------------------------------------------------------------------- 树

static Node *contentOf(Node *d)
{
    Node *c = d->contentHost();
    return c ? c : d;
}

void attach(Node *parent, Node *child)
{
    if (!parent || !child)
        return;
    if (parent->isLeaf())
        return; // 叶子拒绝子节点
    if (child->parent_)
        return; // 已经有父节点
    parent->linkChild(child);
    relayout();
}

void add(Node *parent, Node *child)
{
    attach(contentOf(parent), child);
}

void add(Node *parent, std::initializer_list<Node *> children)
{
    Node *p = contentOf(parent);
    for (Node *c : children)
        attach(p, c);
}

void Node::insertChildAt(int i, Node *child)
{
    if (!child || child->parent_)
        return;
    Node *target = childAt(i);
    if (!target)
    {
        linkChild(child);
        relayout();
        return;
    }
    child->parent_ = this;
    child->prev_ = target->prev_;
    child->next_ = target;
    if (target->prev_)
        target->prev_->next_ = child;
    else
        firstChild_ = child;
    target->prev_ = child;
    relayout();
}

void insert(Node *parent, int i, Node *child)
{
    Node *p = contentOf(parent);
    if (!p || p->isLeaf())
        return;
    p->insertChildAt(i, child);
}

void remove(Node *d)
{
    if (!d)
        return;
    Node *p = d->parent_;
    if (!p)
        return;
    focusLeaving(d);
    p->unlinkChild(d);
    p->childRemoved(d);
    relayout();
}

void removeChildren(Node *parent)
{
    Node *p = contentOf(parent);
    while (p->lastChild())
        remove(p->lastChild());
}

bool isAncestor(Node *a, Node *d)
{
    for (Node *n = d; n; n = n->parent())
    {
        if (n == a)
            return true;
    }
    return false;
}

bool attached(Node *d)
{
    return isAncestor(gRoot, d);
}

// ------------------------------------------------------------------- 布局算法

static Vec2 shrink(Vec2 v, Insets m)
{
    v.x -= m.horizontal();
    v.y -= m.vertical();
    if (v.x < 0)
        v.x = 0;
    if (v.y < 0)
        v.y = 0;
    return v;
}

static float maxf(float a, float b)
{
    return a > b ? a : b;
}

static void resolve(Size s, float avail, float &v, bool &isAuto)
{
    switch (s.mode)
    {
    case SIZE_FIXED:
        v = s.v;
        isAuto = false;
        return;
    case SIZE_RELATIVE:
        if (avail >= unbounded)
        {
            v = 0;
            isAuto = true;
        }
        else
        {
            v = s.v * avail;
            isAuto = false;
        }
        return;
    default:
        v = 0;
        isAuto = true;
        return;
    }
}

static Vec2 contentSize(Node *d, Vec2 max)
{
    if (d->isLayouter())
        return d->measureChildren(max);
    if (d->firstChild() == nullptr)
        return d->measure(max);
    Vec2 s{0, 0};
    for (Node *c = d->firstChild(); c; c = c->nextSibling())
    {
        if (!c->visible())
            continue;
        Insets m = c->margin();
        Vec2 cs = measureNode(c, shrink(max, m));
        Vec2 pos = c->position();
        s.x = maxf(s.x, cs.x + m.horizontal() + pos.x);
        s.y = maxf(s.y, cs.y + m.vertical() + pos.y);
    }
    return s;
}

Vec2 measureNode(Node *d, Vec2 max)
{
    float w = 0, h = 0;
    bool aw = false, ah = false;
    resolve(d->width(), max.x, w, aw);
    resolve(d->height(), max.y, h, ah);
    if (aw || ah)
    {
        Insets p = d->padding();
        Vec2 inner{max.x, max.y};
        if (!aw)
            inner.x = w;
        if (!ah)
            inner.y = h;
        inner.x -= p.horizontal();
        inner.y -= p.vertical();
        Vec2 c = contentSize(d, inner);
        if (aw)
            w = c.x + p.horizontal();
        if (ah)
            h = c.y + p.vertical();
    }
    return Vec2{w, h};
}

void arrangeFree(Node *c, Rect area)
{
    if (!c->visible())
        return;
    Insets m = c->margin();
    Vec2 avail = shrink(Vec2{area.w, area.h}, m);
    Vec2 s = measureNode(c, avail);
    Vec2 a = c->anchor(), o = c->origin(), p = c->position();
    float x = area.x + m.l + a.x * avail.x - o.x * s.x + p.x;
    float y = area.y + m.t + a.y * avail.y - o.y * s.y + p.y;
    arrange(c, Rect{x, y, s.x, s.y});
}

void arrange(Node *d, Rect r)
{
    if ((d->flags_ & (kFlagAnimateLayout | kFlagLaidOut)) == (kFlagAnimateLayout | kFlagLaidOut))
    {
        d->offsetX_.shift(d->rect_.x - r.x);
        d->offsetY_.shift(d->rect_.y - r.y);
    }
    d->rect_ = r;
    d->flags_ |= kFlagLaidOut;
    Rect content = d->contentRect();
    if (d->isLayouter())
    {
        d->arrangeChildren(content);
        return;
    }
    for (Node *c = d->firstChild(); c;)
    {
        Node *n = c->nextSibling();
        arrangeFree(c, content);
        c = n;
    }
}

// ---------------------------------------------------------------------- Flex

// 选项必须写成 ui::apply：裸 apply 会让 ADL 把 initializer_list 带进 std 里，
// C++17 下直接命中 std::apply（它接受任意两个参数），选成完全匹配，选项就
// 一个都跑不上了。ESP32 的 gnu++11 里没有 std::apply 才一直没暴露。
Flex::Flex(Axis a, std::initializer_list<Option> opts) : axis_(a)
{
    ui::apply(this, opts);
}

void Flex::setAxis(Axis a)
{
    if (axis_ != a)
    {
        axis_ = a;
        relayout();
    }
}

void Flex::setGap(float g)
{
    if (gap_ != g)
    {
        gap_ = g;
        relayout();
    }
}

void Flex::setMainAlign(MainAlign m)
{
    if (main_ != m)
    {
        main_ = m;
        relayout();
    }
}

void Flex::setCrossAlign(CrossAlign c)
{
    if (cross_ != c)
    {
        cross_ = c;
        relayout();
    }
}

// split / join 在 x/y 与主轴/交叉轴之间换算。
Vec2 Flex::split(Vec2 v) const
{
    return axis_ == Horizontal ? v : Vec2{v.y, v.x};
}

Vec2 Flex::join(float m, float c) const
{
    return axis_ == Horizontal ? Vec2{m, c} : Vec2{c, m};
}

void Flex::margins(const Node *n, float &mm, float &cm, float &lead, float &crossLead) const
{
    Insets m = n->margin();
    if (axis_ == Horizontal)
    {
        mm = m.horizontal();
        cm = m.vertical();
        lead = m.l;
        crossLead = m.t;
    }
    else
    {
        mm = m.vertical();
        cm = m.horizontal();
        lead = m.t;
        crossLead = m.l;
    }
}

bool Flex::crossAuto(const Node *n) const
{
    return axis_ == Horizontal ? n->height().mode == SIZE_AUTO : n->width().mode == SIZE_AUTO;
}

Vec2 Flex::measureChildren(Vec2 max)
{
    float main = 0, cross = 0;
    int n = 0;
    for (Node *c = firstChild(); c; c = c->nextSibling())
    {
        if (!c->visible())
            continue;
        Vec2 s = measureNode(c, shrink(max, c->margin()));
        float mm, cm, lead, crossLead;
        margins(c, mm, cm, lead, crossLead);
        (void)lead;
        (void)crossLead;
        Vec2 sc = split(s);
        main += sc.x + mm;
        cross = maxf(cross, sc.y + cm);
        n++;
    }
    if (n > 1)
        main += gap_ * float(n - 1);
    return join(main, cross);
}

void Flex::arrangeChildren(Rect content)
{
    Vec2 avail = split(Vec2{content.w, content.h});
    float availMain = avail.x, availCross = avail.y;
    float used = 0, grow = 0;
    int n = 0;
    for (Node *c = firstChild(); c; c = c->nextSibling())
    {
        if (!c->visible())
            continue;
        c->setMeasured(measureNode(c, shrink(Vec2{content.w, content.h}, c->margin())));
        float mm, cm, lead, crossLead;
        margins(c, mm, cm, lead, crossLead);
        (void)cm;
        (void)lead;
        (void)crossLead;
        used += split(c->measured()).x + mm;
        grow += c->grow();
        n++;
    }
    float gap = gap_;
    if (n > 1)
        used += gap * float(n - 1);
    float freeSpace = availMain - used;
    float pos = 0;
    if (grow == 0 && freeSpace > 0)
    {
        switch (main_)
        {
        case MainCenter:
            pos = freeSpace / 2;
            break;
        case MainEnd:
            pos = freeSpace;
            break;
        case MainSpaceBetween:
            if (n > 1)
                gap += freeSpace / float(n - 1);
            break;
        default:
            break;
        }
    }
    // 有子节点伸展时它们顺带吸收溢出，于是过长的文字会缩起来，而不是把
    // 兄弟节点挤出这一行。
    if (grow == 0)
        freeSpace = maxf(freeSpace, 0);

    for (Node *c = firstChild(); c; c = c->nextSibling())
    {
        if (!c->visible())
            continue;
        float mm, cm, lead, crossLead;
        margins(c, mm, cm, lead, crossLead);
        Vec2 sm = split(c->measured());
        float mainSize = sm.x, crossSize = sm.y;
        if (grow > 0)
            mainSize = maxf(mainSize + freeSpace * c->grow() / grow, 0);
        float room = availCross - cm;
        float cp = 0;
        if (cross_ == CrossStretch && crossAuto(c))
        {
            crossSize = room;
        }
        else if (cross_ == CrossCenter)
        {
            cp = (room - crossSize) / 2;
        }
        else if (cross_ == CrossEnd)
        {
            cp = room - crossSize;
        }
        Vec2 o = join(pos + lead, cp + crossLead);
        Vec2 s = join(mainSize, crossSize);
        arrange(c, Rect{content.x + o.x, content.y + o.y, s.x, s.y});
        pos += mainSize + mm + gap;
    }
}

Flex *newRow(std::initializer_list<Option> opts)
{
    return new Flex(Horizontal, opts);
}
Flex *newColumn(std::initializer_list<Option> opts)
{
    return new Flex(Vertical, opts);
}

// --------------------------------------------------------------------- Stack

Stack::Stack(std::initializer_list<Option> opts)
{
    ui::apply(this, opts);
}

// ---------------------------------------------------------------------- Grid

Grid::Grid(std::initializer_list<Option> opts)
{
    setColumns({frTrack(1)});
    ui::apply(this, opts);
}

void Grid::setColumns(const Track *tracks, int count)
{
    colCount_ = 0;
    for (int i = 0; i < count && colCount_ < kMaxColumns; i++)
        cols_[colCount_++] = tracks[i];
    if (colCount_ == 0)
        cols_[colCount_++] = frTrack(1);
    relayout();
}

void Grid::setGap(float g)
{
    if (gap_ != g)
    {
        gap_ = g;
        relayout();
    }
}

void Grid::columnWidths(float avail)
{
    float fixed = 0, fr = 0;
    for (int i = 0; i < colCount_; i++)
    {
        fixed += cols_[i].px;
        fr += cols_[i].fr;
    }
    float rest = maxf(avail - fixed - gap_ * float(colCount_ - 1), 0);
    for (int i = 0; i < colCount_; i++)
    {
        widths_[i] = cols_[i].px;
        if (fr > 0)
            widths_[i] += rest * cols_[i].fr / fr;
    }
}

Vec2 Grid::measureChildren(Vec2 max)
{
    columnWidths(max.x);
    float w = gap_ * float(colCount_ - 1);
    for (int i = 0; i < colCount_; i++)
        w += widths_[i];

    float y = 0, rowH = 0;
    int col = 0;
    for (Node *c = firstChild(); c; c = c->nextSibling())
    {
        if (!c->visible())
            continue;
        Vec2 ms = measureNode(c, shrink(Vec2{widths_[col], max.y}, c->margin()));
        rowH = maxf(rowH, ms.y + c->margin().vertical());
        col++;
        if (col == colCount_)
        {
            y += rowH + gap_;
            rowH = 0;
            col = 0;
        }
    }
    if (col > 0)
        y += rowH + gap_;
    return Vec2{w, maxf(y - gap_, 0)};
}

void Grid::arrangeChildren(Rect content)
{
    columnWidths(content.w);

    // placeRow 把一行子节点按已知的行高分派到各自的格子里。
    auto placeRow = [&](Node *start, Node *stop, float y, float h) {
        float x = content.x;
        int col = 0;
        for (Node *c = start; c != stop; c = c->nextSibling())
        {
            if (!c->visible())
                continue;
            Insets m = c->margin();
            Vec2 s = c->measured();
            if (c->width().mode == SIZE_AUTO)
                s.x = widths_[col] - m.horizontal();
            if (c->height().mode == SIZE_AUTO)
                s.y = h - m.vertical();
            arrange(c, Rect{x + m.l, content.y + y + m.t, s.x, s.y});
            x += widths_[col] + gap_;
            col++;
        }
    };

    float y = 0, rowH = 0;
    int col = 0;
    Node *start = firstChild();
    for (Node *c = firstChild(); c;)
    {
        Node *next = c->nextSibling();
        if (!c->visible())
        {
            c = next;
            continue;
        }
        Insets m = c->margin();
        c->setMeasured(measureNode(c, shrink(Vec2{widths_[col], content.h}, m)));
        rowH = maxf(rowH, c->measured().y + m.vertical());
        col++;
        if (col == colCount_)
        {
            placeRow(start, next, y, rowH);
            y += rowH + gap_;
            rowH = 0;
            col = 0;
            start = next;
        }
        c = next;
    }
    if (col > 0)
        placeRow(start, nullptr, y, rowH);
}

// --------------------------------------------------------------------- 选项

void apply(Node *d, std::initializer_list<Option> opts)
{
    for (const Option &o : opts)
    {
        if (o)
            o(d);
    }
}

Option width(Size s)
{
    return [s](Node *d) { d->setWidth(s); };
}

Option height(Size s)
{
    return [s](Node *d) { d->setHeight(s); };
}

Option fill()
{
    return [](Node *d) {
        d->setWidth(rel(1));
        d->setHeight(rel(1));
    };
}

Option fillWidth()
{
    return [](Node *d) { d->setWidth(rel(1)); };
}

Option fillHeight()
{
    return [](Node *d) { d->setHeight(rel(1)); };
}

Option anchor(Vec2 v)
{
    return [v](Node *d) { d->setAnchor(v); };
}

Option origin(Vec2 v)
{
    return [v](Node *d) { d->setOrigin(v); };
}

Option align(Vec2 v)
{
    return [v](Node *d) {
        d->setAnchor(v);
        d->setOrigin(v);
    };
}

Option position(float x, float y)
{
    return [x, y](Node *d) { d->setPosition(Vec2{x, y}); };
}

Option margin(Insets i)
{
    return [i](Node *d) { d->setMargin(i); };
}

Option padding(Insets i)
{
    return [i](Node *d) { d->setPadding(i); };
}

Option grow(float g)
{
    return [g](Node *d) { d->setGrow(g); };
}

Option focusable()
{
    return [](Node *d) { d->setFocusable(true); };
}

Option hidden()
{
    return [](Node *d) { d->setVisible(false); };
}

Option clip()
{
    return [](Node *d) { d->setClip(true); };
}

Option animateLayout()
{
    return [](Node *d) { d->setAnimateLayout(true); };
}

Option background(const gfx::Color &c)
{
    return [c](Node *d) { d->setBackground(c); };
}

Option border(const gfx::Color &c)
{
    return [c](Node *d) { d->setBorder(c); };
}

Option radius(float r)
{
    return [r](Node *d) { d->setRadius(r); };
}

Option gap(float g)
{
    return [g](Node *d) {
        if (d->nodeTag() == kTagFlex)
        {
            static_cast<Flex *>(d)->setGap(g);
            return;
        }
        if (d->nodeTag() == kTagGrid)
            static_cast<Grid *>(d)->setGap(g);
    };
}

Option main(MainAlign m)
{
    return [m](Node *d) {
        if (d->nodeTag() == kTagFlex)
            static_cast<Flex *>(d)->setMainAlign(m);
    };
}

Option cross(CrossAlign c)
{
    return [c](Node *d) {
        if (d->nodeTag() == kTagFlex)
            static_cast<Flex *>(d)->setCrossAlign(c);
    };
}

Option columns(std::initializer_list<Track> tracks)
{
    // 捕获一份定长副本，避免在 lambda 里依赖 initializer_list 的生命周期。
    struct List
    {
        Track items[6];
        int count;
    } list;
    list.count = 0;
    for (const Track &t : tracks)
    {
        if (list.count >= 6)
            break;
        list.items[list.count++] = t;
    }
    return [list](Node *d) {
        if (d->nodeTag() == kTagGrid)
            static_cast<Grid *>(d)->setColumns(list.items, list.count);
    };
}

Option children(std::initializer_list<Node *> nodes)
{
    return [nodes](Node *d) { add(d, nodes); };
}

} // namespace ui
} // namespace lilty
