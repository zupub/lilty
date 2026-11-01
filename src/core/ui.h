// ui: 节点树、布局（Flex / Grid / Stack）、页面与弹窗的层栈、焦点、输入、
// 光标、主题。对应 lilt 的 ui 包。
//
// Go 版靠接口 + 类型断言发现节点能力，这里改成单基类的虚函数：默认什么
// 都不做，需要哪种能力就覆写哪个方法，等价而且更好懂。
#pragma once

#include <initializer_list>
#include <functional>
#include <stdint.h>

#include "core/anim.h"
#include "core/gfx.h"

namespace lilty
{
namespace ui
{

// ---------------------------------------------------------------- 坐标与尺寸

// Vec2 是逻辑像素下的点或尺寸。布局用 float32 让动画位置保持平滑，绘制时
// 再换算成物理像素。
struct Vec2
{
    float x, y;
};

inline bool operator==(const Vec2 &a, const Vec2 &b)
{
    return a.x == b.x && a.y == b.y;
}
inline bool operator!=(const Vec2 &a, const Vec2 &b)
{
    return !(a == b);
}

// 锚点和原点的对齐位置。
extern const Vec2 TopLeft;
extern const Vec2 TopCenter;
extern const Vec2 TopRight;
extern const Vec2 CenterLeft;
extern const Vec2 Center;
extern const Vec2 CenterRight;
extern const Vec2 BottomLeft;
extern const Vec2 BottomCenter;
extern const Vec2 BottomRight;

// Rect 是逻辑像素下的布局矩形。
struct Rect
{
    float x, y, w, h;

    float right() const
    {
        return x + w;
    }
    float bottom() const
    {
        return y + h;
    }
    Vec2 center() const
    {
        return Vec2{x + w / 2, y + h / 2};
    }
    // pixels 换算成物理像素并各自取整，相邻矩形取整后仍然相邻。
    gfx::Rect pixels() const;
};

// Insets 是外边距或内边距，单位逻辑像素。
struct Insets
{
    float l, t, r, b;

    float horizontal() const
    {
        return l + r;
    }
    float vertical() const
    {
        return t + b;
    }
    Insets scale(float f) const
    {
        return Insets{l * f, t * f, r * f, b * f};
    }
};

inline Insets all(float v)
{
    return Insets{v, v, v, v};
}
inline Insets xy(float x, float y)
{
    return Insets{x, y, x, y};
}
inline Insets ltrb(float l, float t, float r, float b)
{
    return Insets{l, t, r, b};
}

enum SizeMode : uint8_t
{
    SIZE_AUTO,
    SIZE_FIXED,
    SIZE_RELATIVE
};

// Size 是节点一个轴上的尺寸：固定逻辑像素、父内容区的比例，或按内容自适应。
struct Size
{
    SizeMode mode;
    float v;

    bool operator==(const Size &o) const
    {
        return mode == o.mode && v == o.v;
    }
    bool operator!=(const Size &o) const
    {
        return !(*this == o);
    }
};

inline Size px(float v)
{
    return Size{SIZE_FIXED, v};
}
inline Size rel(float f)
{
    return Size{SIZE_RELATIVE, f};
}
inline Size autoSize()
{
    return Size{SIZE_AUTO, 0};
}

// unbounded 是滚动轴上的可用空间。相对尺寸在这里退化成自适应，因为无穷大
// 的比例没有意义。
constexpr float unbounded = 16777216.0f;

// Track 是网格列宽：固定像素或剩余空间的份额。
struct Track
{
    float px, fr;
};
inline Track fixedTrack(float p)
{
    return Track{p, 0};
}
inline Track frTrack(float f)
{
    return Track{0, f};
}

// ------------------------------------------------------------------ 缩放换算

constexpr int16_t kBaseW = 128;
constexpr int16_t kBaseH = 64;

int16_t round16(float x);
// scale 是每个逻辑像素对应的物理像素：min(宽/128, 高/64) 再乘 zoom。
float scale();
// dp 把逻辑像素换算成物理像素并取整。
int16_t dp(float v);
// setZoom 在屏幕尺寸给出的比例上再乘一个倍数，需在 lilty::begin 之前调用。
void setZoom(float f);
// scaleFor 是 w x h 的屏幕在当前 zoom 下的 scale，用来在启动前挑字体。
float scaleFor(int16_t w, int16_t h);
// lineHeight / textWidth 返回逻辑像素下的行高与文本宽度。
float lineHeight(const gfx::Font *f);
float textWidth(const gfx::Font *f, const char *s);

// --------------------------------------------------------------------- 主题

// Theme 是组件绘制时用的颜色和字体。
struct Theme
{
    gfx::Color background;
    gfx::Color foreground;
    // accent 画在不能反色的屏上，代替反色光标。
    gfx::Color accent;
    // muted 用于提示和禁用文字。
    gfx::Color muted;
    const gfx::Font *font;
    const gfx::Font *smallFont;
    // radius 是带边框控件的圆角，单位逻辑像素。
    float radius;
    // rowPadding 是列表行、字段、标题和说明文字的内边距。
    Insets rowPadding;
    // buttonPadding 是按钮文字与边框之间的留白。
    Insets buttonPadding;
    // gap 是抽屉、对话框和带内边距列表里控件之间的间距。
    float gap;
    // panelPadding 让控件与外围边框留出距离。
    Insets panelPadding;
};

extern Theme theme;

// 光标运动参数。朝向目标的那条边用 cursorLead，另一条用更慢的 cursorTrail，
// 于是光标朝目的地拉长、到位时收拢。
extern anim::Spring cursorLead;
extern anim::Spring cursorTrail;
extern anim::Spring cursorRelease;
extern anim::Spring cursorLeap;
extern anim::Spring cursorLean;

// --------------------------------------------------------------------- 输入

enum class Key : uint8_t
{
    None = 0,
    Up,
    Down,
    Left,
    Right,
    OK,
    Back,
    Menu,
    Backspace,
    Char,
    Count
};

constexpr int kKeyCount = int(Key::Count);

inline bool isArrow(Key k)
{
    return k >= Key::Up && k <= Key::Right;
}

enum Phase : uint8_t
{
    Press,
    Repeat,
    Release
};

struct KeyEvent
{
    Key key;
    Phase phase;
    char rune;
    // canceled 标记不该触发动作的松手，例如触摸变成拖动之后。
    bool canceled;
    // repeated 标记连发之后的松手，动作已经在连发里做过了。
    bool repeated;

    KeyEvent() : key(Key::None), phase(Press), rune(0), canceled(false), repeated(false)
    {
    }
    KeyEvent(Key k, Phase p) : key(k), phase(p), rune(0), canceled(false), repeated(false)
    {
    }

    // fires 报告这次事件是否该执行按键的动作：未取消、非连发之后的松手，
    // 或者按住期间每一次连发。按下本身只给反馈。
    bool fires() const
    {
        return phase == Repeat || (phase == Release && !canceled && !repeated);
    }
};

// ---------------------------------------------------------------- 节点与布局

class Node;
using Option = std::function<void(Node *)>;

// flags 是节点的布尔状态位。
enum NodeFlag : uint16_t
{
    kFlagHidden = 1 << 0,
    kFlagFocusable = 1 << 1,
    kFlagClip = 1 << 2,
    kFlagAnimateLayout = 1 << 3,
    kFlagLaidOut = 1 << 4,
    kFlagBackground = 1 << 5,
    kFlagBorder = 1 << 6,
};

// NodeTag 是节点的类型标签。ESP32 的 Arduino 编译带 -fno-rtti，dynamic_cast
// 用不了，所以上下转一律先比标签再 static_cast。
enum NodeTag : uint8_t
{
    kTagNode = 0,
    kTagFlex,
    kTagGrid,
    kTagStack,
    kTagScroll,
};

// Node 是树里的任何东西，同时承担 Go 版 Box 的职责：树链接、布局属性、
// 可动画的偏移。能力通过覆写虚函数获得。
class Node
{
  public:
    Node()
    {
    }
    virtual ~Node();

    // --- 树 ---
    Node *parent() const
    {
        return parent_;
    }
    Node *firstChild() const
    {
        return firstChild_;
    }
    Node *lastChild() const
    {
        return lastChild_;
    }
    Node *nextSibling() const
    {
        return next_;
    }
    Node *prevSibling() const
    {
        return prev_;
    }
    int childCount() const;

    // --- 尺寸与位置 ---
    Size width() const
    {
        return width_;
    }
    Size height() const
    {
        return height_;
    }
    void setWidth(Size s);
    void setHeight(Size s);

    Vec2 anchor() const
    {
        return anchor_;
    }
    void setAnchor(Vec2 v);
    Vec2 origin() const
    {
        return origin_;
    }
    void setOrigin(Vec2 v);
    Vec2 position() const
    {
        return pos_;
    }
    void setPosition(Vec2 v);

    Insets margin() const
    {
        return margin_;
    }
    void setMargin(Insets i);
    Insets padding() const
    {
        return padding_;
    }
    void setPadding(Insets i);

    float grow() const
    {
        return grow_;
    }
    void setGrow(float g);

    bool visible() const
    {
        return (flags_ & kFlagHidden) == 0;
    }
    // setVisible 隐藏或显示节点。隐藏的节点不占位置、不绘制、不能获得焦点。
    void setVisible(bool v);

    bool focusable() const
    {
        return (flags_ & kFlagFocusable) != 0;
    }
    void setFocusable(bool f);
    // setClip 把节点和子孙的绘制限制在自己的矩形内。
    void setClip(bool c);
    // setAnimateLayout 让布局引起的位置变化滑过去而不是瞬移。
    void setAnimateLayout(bool a)
    {
        set(kFlagAnimateLayout, a);
    }

    void setBackground(const gfx::Color &c);
    void setBorder(const gfx::Color &c);
    // setRadius 设置背景与边框的圆角，单位逻辑像素。
    void setRadius(float r);
    bool bordered() const
    {
        return (flags_ & kFlagBorder) != 0;
    }
    bool clipEnabled() const
    {
        return (flags_ & kFlagClip) != 0;
    }
    bool hasBackground() const
    {
        return (flags_ & kFlagBackground) != 0;
    }
    gfx::Color backgroundColor() const
    {
        return bg_;
    }
    gfx::Color borderColor() const
    {
        return border_;
    }
    float radiusValue() const
    {
        return radius_;
    }

    // rect 是相对父节点的、不含偏移的布局矩形。
    Rect rect() const
    {
        return rect_;
    }
    // contentRect 是内边距之内的区域，位于节点自身坐标系。
    Rect contentRect() const;

    // measured 是 Layouter 摆子节点时用的临时测量结果。
    Vec2 measured() const
    {
        return measured_;
    }
    void setMeasured(Vec2 v)
    {
        measured_ = v;
    }

    // offsetX / offsetY 是叠加在布局之上的可动画平移。子节点相对父节点绘制，
    // 所以会自然继承。
    anim::Value *offsetX()
    {
        return &offsetX_;
    }
    anim::Value *offsetY()
    {
        return &offsetY_;
    }
    const anim::Value *offsetX() const
    {
        return &offsetX_;
    }
    const anim::Value *offsetY() const
    {
        return &offsetY_;
    }

    // --- 能力（覆写即获得） ---
    // nodeTag 用来在不依赖 RTTI 的前提下认出具体类型。
    virtual int nodeTag() const
    {
        return kTagNode;
    }
    // draw 画自己的内容，在子节点之前。
    virtual void draw(gfx::Canvas &c)
    {
        (void)c;
    }
    // drawOver 在子节点之后画。
    virtual void drawOver(gfx::Canvas &c)
    {
        (void)c;
    }
    // update 每帧推进一次逻辑。
    virtual void update(float dt)
    {
        (void)dt;
    }
    // measure 返回叶子在 max 内的内容尺寸，不含内边距。
    virtual Vec2 measure(Vec2 max)
    {
        (void)max;
        return Vec2{0, 0};
    }
    // isLayouter 为真时由节点自己摆放子节点，用下面两个方法。
    virtual bool isLayouter() const
    {
        return false;
    }
    virtual Vec2 measureChildren(Vec2 max)
    {
        (void)max;
        return Vec2{0, 0};
    }
    virtual void arrangeChildren(Rect content)
    {
        (void)content;
    }

    // contentHost 让 add / removeChildren 把子节点挂到内层节点上，于是 List
    // 可以接收行，而滚动机件保持私有。
    virtual Node *contentHost()
    {
        return this;
    }
    // isLeaf 标记拒绝子节点的节点。
    virtual bool isLeaf() const
    {
        return false;
    }
    virtual void childRemoved(Node *child)
    {
        (void)child;
    }

    virtual void focusChanged(bool focused)
    {
        (void)focused;
    }
    // handleKey 接收从焦点节点冒泡上来的按键。返回 true 表示认领该键。
    virtual bool handleKey(KeyEvent &e)
    {
        (void)e;
        return false;
    }
    // previewKey 在按键从根往下传时先看到它，容器可以借此拦截。
    virtual bool previewKey(KeyEvent &e)
    {
        (void)e;
        return false;
    }
    // reveal 在子孙获得焦点时通知祖先，让它把目标滚进视野。
    virtual void reveal(Node *d)
    {
        (void)d;
    }
    // arrowScroll 在焦点移动之前被祖先依次询问，返回 true 表示消费掉这个键。
    virtual bool arrowScroll(KeyEvent &e, Node *next)
    {
        (void)e;
        (void)next;
        return false;
    }
    // cursorShape 让焦点节点改变光标的矩形（逻辑屏幕坐标）。
    virtual Rect cursorShape(Rect r)
    {
        return r;
    }

  protected:
    void set(uint16_t f, bool on);

  private:
    friend void attach(Node *parent, Node *child);
    friend void insert(Node *parent, int i, Node *child);
    friend void remove(Node *d);
    friend void arrange(Node *d, Rect r);

    // linkChild / unlinkChild 维护兄弟链表，链表顺序就是绘制顺序。
    void linkChild(Node *child);
    void unlinkChild(Node *child);
    void insertChildAt(int i, Node *child);
    Node *childAt(int i) const;

    Node *parent_ = nullptr;
    Node *firstChild_ = nullptr;
    Node *lastChild_ = nullptr;
    Node *next_ = nullptr;
    Node *prev_ = nullptr;

    Size width_{SIZE_AUTO, 0};
    Size height_{SIZE_AUTO, 0};
    Vec2 anchor_{0, 0};
    Vec2 origin_{0, 0};
    Vec2 pos_{0, 0};
    Insets margin_{0, 0, 0, 0};
    Insets padding_{0, 0, 0, 0};
    float grow_ = 0;
    uint16_t flags_ = 0;
    gfx::Color bg_{0, 0, 0, 255};
    gfx::Color border_{0, 0, 0, 255};
    float radius_ = 0;

    Rect rect_{0, 0, 0, 0};
    Vec2 measured_{0, 0};

    anim::Value offsetX_;
    anim::Value offsetY_;
};

// 重新布局并重绘（下一帧整树重排）。
void relayout();
// 只安排重绘。
void invalidate();

// add 把子节点追加到 parent，或追加到它的内层内容节点。
void add(Node *parent, Node *child);
void add(Node *parent, std::initializer_list<Node *> children);
// insert 把 child 放到 parent 子节点的第 i 个位置。
void insert(Node *parent, int i, Node *child);
// attach 直接追加，绕过内容节点。组件用它搭建内部结构。
void attach(Node *parent, Node *child);
// remove 把节点从父节点摘下并通知父节点。节点可以移除自己。
void remove(Node *d);
void removeChildren(Node *parent);

// isAncestor 报告 a 是 d 本身还是包含 d。
bool isAncestor(Node *a, Node *d);
// attached 报告 d 是否在活的树里。
bool attached(Node *d);

// measureNode 返回 d 在 max 内想要的尺寸。
Vec2 measureNode(Node *d, Vec2 max);
// arrange 给 d 最终的矩形（相对父节点）并摆放它的子节点。
void arrange(Node *d, Rect r);
// arrangeFree 按锚点、原点和位置把 c 放进 area，是非 Layouter 节点的默认方式。
void arrangeFree(Node *c, Rect area);

// restRect 是 d 的动画稳定下来后在屏幕上的矩形。
Rect restRect(Node *d);
// firstFocusable 返回树序上第一个可聚焦节点。
Node *firstFocusable(Node *d);
// findFocus 从 from 出发沿 dir 找到焦点应该去的节点（Android FocusFinder）。
Node *findFocus(Node *scope, Node *from, Key dir);

// ------------------------------------------------------------------ 容器组件

enum Axis : uint8_t
{
    Horizontal = 0,
    Vertical = 1
};

// MainAlign 在没有子节点伸展时分配主轴上的剩余空间。
enum MainAlign : uint8_t
{
    MainStart,
    MainCenter,
    MainEnd,
    MainSpaceBetween
};

// CrossAlign 摆放交叉轴上的子节点。
enum CrossAlign : uint8_t
{
    // CrossStretch 让交叉轴上尺寸为 Auto 的子节点填满。
    CrossStretch,
    CrossStart,
    CrossCenter,
    CrossEnd,
};

// Flex 沿一个轴排列子节点。
class Flex : public Node
{
  public:
    explicit Flex(Axis a = Vertical, std::initializer_list<Option> opts = {});

    Axis axis() const
    {
        return axis_;
    }
    void setAxis(Axis a);
    float gap() const
    {
        return gap_;
    }
    // setGap 设置子节点间距，单位逻辑像素。
    void setGap(float g);
    void setMainAlign(MainAlign m);
    void setCrossAlign(CrossAlign c);
    MainAlign mainAlign() const
    {
        return main_;
    }
    CrossAlign crossAlign() const
    {
        return cross_;
    }

    int nodeTag() const override
    {
        return kTagFlex;
    }
    Vec2 measureChildren(Vec2 max) override;
    void arrangeChildren(Rect content) override;
    bool isLayouter() const override
    {
        return true;
    }

  private:
    Vec2 split(Vec2 v) const;
    Vec2 join(float m, float c) const;
    void margins(const Node *n, float &mm, float &cm, float &lead, float &crossLead) const;
    bool crossAuto(const Node *n) const;

    Axis axis_;
    float gap_ = 0;
    MainAlign main_ = MainStart;
    CrossAlign cross_ = CrossStretch;
};

Flex *newRow(std::initializer_list<Option> opts = {});
Flex *newColumn(std::initializer_list<Option> opts = {});

// Stack 是按锚点摆放子节点的普通容器。
class Stack : public Node
{
  public:
    explicit Stack(std::initializer_list<Option> opts = {});

    int nodeTag() const override
    {
        return kTagStack;
    }
};

// Grid 按行把子节点放进列里。每行的高度取最高的子节点，Auto 尺寸的子节点
// 填满自己的格子。
class Grid : public Node
{
  public:
    explicit Grid(std::initializer_list<Option> opts = {});

    void setColumns(std::initializer_list<Track> tracks)
    {
        setColumns(tracks.begin(), int(tracks.size()));
    }
    void setColumns(const Track *tracks, int count);
    void setGap(float g);
    float gap() const
    {
        return gap_;
    }

    int nodeTag() const override
    {
        return kTagGrid;
    }
    Vec2 measureChildren(Vec2 max) override;
    void arrangeChildren(Rect content) override;
    bool isLayouter() const override
    {
        return true;
    }

  private:
    static constexpr int kMaxColumns = 6;
    void columnWidths(float avail);

    Track cols_[kMaxColumns];
    int colCount_ = 0;
    float widths_[kMaxColumns];
    float gap_ = 0;
};

// ------------------------------------------------------------------ 选项

// apply 在构造末尾跑一遍选项。
void apply(Node *d, std::initializer_list<Option> opts);

Option width(Size s);
Option height(Size s);
// fill 让节点填满父内容区。
Option fill();
Option fillWidth();
Option fillHeight();
Option anchor(Vec2 v);
Option origin(Vec2 v);
// align 同时设置锚点和原点，是固定到某个角或某条边的常用写法。
Option align(Vec2 v);
Option position(float x, float y);
Option margin(Insets i);
Option padding(Insets i);
Option grow(float g);
Option focusable();
Option hidden();
Option clip();
Option animateLayout();
Option background(const gfx::Color &c);
Option border(const gfx::Color &c);
Option radius(float r);
// gap 设置 Flex 或 Grid 的子节点间距。
Option gap(float g);
Option main(MainAlign m);
Option cross(CrossAlign c);
Option columns(std::initializer_list<Track> tracks);
Option children(std::initializer_list<Node *> nodes);

// ------------------------------------------------------------------ 层栈

// Popup 是 open 打开的层的位置和进出方向。
enum Popup : uint8_t
{
    // PopupDrawerBottom 从底边升起，高度按内容。
    PopupDrawerBottom = 0,
    PopupDrawerTop,
    PopupDrawerLeft,
    PopupDrawerRight,
    // PopupDialog 落到屏幕中央。
    PopupDialog,
    // PopupToast 从底部浮起，不抢焦点也不接按键。
    PopupToast,
    // PopupFullPage 是 push 进来的整屏。
    PopupFullPage,
};

// push 把 p 作为新的一屏从右侧推入。
void push(Node *p);
// pop 关掉最上面一层，页面或弹窗。根页面留着。
bool pop();
// open 把 n 作为弹窗打开；popup 决定它从哪条边进来。
void open(Node *n, Popup popup);
// close 关掉正在显示 n 的那一层。
bool close(Node *n);

// ------------------------------------------------------------------ 焦点与输入

void setFocus(Node *d);
Node *focused();
// emit 投递一个按键事件。
void emit(Key k, Phase p);
// emitRune 投递一个输入字符。
void emitRune(char r);
// doNow 在帧与帧之间执行 fn。单线程实现里它就直接调用 fn，保留 API 形状。
void doNow(const std::function<void()> &fn);

// screenWidth / screenHeight 是绑定屏幕的逻辑尺寸。
float screenWidth();
float screenHeight();

// KeyPoller 把原始按键电平变成消抖后的按下、连发、松开，连发会逐渐加速。
class KeyPoller
{
  public:
    uint8_t debounce = 2;
    float repeatDelay = 0.2f;
    float repeatInterval = 0.07f;
    float minInterval = 0.03f;
    // accel 在每次连发后乘到间隔上。
    float accel = 0.9f;

    // poll 给 k 喂一个采样，dt 是距上次采样的时间。
    void poll(Key k, bool down, float dt);

  private:
    struct State
    {
        bool raw = false, down = false;
        uint8_t stable = 0;
        float held = 0, next = 0, iv = 0;
    };
    State keys_[kKeyCount];
};

// 帧：由 lilty 驱动。返回是否重绘过。
bool frame(float dt);

// boot 绑定屏幕、算出缩放，并把 first 作为根页面推进层栈。
void boot(gfx::Screen *s, Node *first);
// reset 清空所有页面和弹窗，回到刚 boot 的状态。
void reset();

} // namespace ui
} // namespace lilty
