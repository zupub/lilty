// widget: 基于 ui 的标准组件。对应 lilt 的 widget 包。
//
// Go 版靠接口加类型断言发现能力，这里用普通继承：Label 是最简单的叶子，
// 其余交互组件都继承 ListItem，于是自动获得“行 + 标题 + 尾部控件 + 按键
// 激活”的形状。
//
// 最大的差别是回调：没有 goroutine，处理器在按键处理里同步跑完，所以
// “加载中”这一刻观察不到，界面也不会出现转圈的三个点。处理器返回 false
// 表示失败，组件会回滚状态并抖一下。
//
// 所有权也是显式的：凡是挂到树上的节点都必须用 new 创建，由父节点在析构
// 时递归 delete。
#pragma once

#include <functional>
#include <initializer_list>
#include <stdint.h>

#include "core/ui.h"

namespace lilty
{
namespace widget
{

using ui::Flex;
using ui::Key;
using ui::KeyEvent;
using ui::Node;
using ui::Option;
using ui::Rect;
using ui::Vec2;

// kLoadingDelay 是处理器跑多久之后才值得显示等待。
constexpr float kLoadingDelay = 0.15f;
// kTextCap 是 Label 文本缓冲的长度，超出的部分会被截掉。
constexpr int kTextCap = 64;

// Action 是一次激活要做的事，返回 false 表示失败。
using Action = std::function<bool()>;
// Done 在动作结束后拿到结果。
using Done = std::function<void(bool ok)>;

// shake 是“操作失败”的共用动作：把控件横向甩一下再弹回来。
void shake(Node *d);

// Control 保存控件的事件处理器和它驱动的状态。
class Control
{
  public:
    // onClick 在控件被激活时运行。
    Action onClick;

    bool busy() const
    {
        return busy_;
    }
    // loading 报告处理器是否已经跑久到该显示等待了。同步实现下永远是假。
    bool loading() const
    {
        return busy_ && elapsed_ >= kLoadingDelay;
    }
    // tick 推进加载计时。
    void tick(float dt);
    // run 执行 call，已有动作在跑时返回 false。
    bool run(const Action &call, const Done &done = Done());

  protected:
    bool busy_ = false;
    float elapsed_ = 0;
};

// ---------------------------------------------------------------- Label

// Marquee 说明过长的文字什么时候滚动。凡溢出的标签都滚动，模式只挑是否
// 要等焦点。
enum Marquee : uint8_t
{
    // MarqueeFocused 只在标签或它的祖先有焦点时滚动，于是列表只动正在读的
    // 那一行。
    MarqueeFocused = 0,
    MarqueeAlways,
};

// Label 是一行文字。它在自己的框里垂直居中，水平位置由 align 决定，
// 0 靠左，1 靠右。
class Label : public Node
{
  public:
    Label(const char *text = "", std::initializer_list<Option> opts = {});

    bool isLeaf() const override
    {
        return true;
    }

    const char *text() const
    {
        return text_;
    }
    void setText(const char *s);
    // font 返回实际使用的字体，没设过就是主题字体。
    const gfx::Font *font() const;
    void setFont(const gfx::Font *f);
    // setColor 覆盖主题的前景色。
    void setColor(const gfx::Color &c);
    void setAlign(float x);
    void setMarquee(Marquee m);

    Vec2 measure(Vec2 max) override;
    void update(float dt) override;
    void draw(gfx::Canvas &c) override;

  private:
    // 跑马灯的状态：两端各停一会儿，去程匀速，回程交给弹簧。
    enum Step : uint8_t
    {
        HoldStart,
        Forward,
        HoldEnd,
        Back
    };

    void remeasure();
    void restart();
    // overflow 是文字超出内容框多少，逻辑像素。
    float overflow() const;
    bool scrolling() const;
    float shift(float over) const;

    char text_[kTextCap];
    const gfx::Font *font_ = nullptr;
    bool hasColor_ = false;
    gfx::Color color_{0, 0, 0, 255};
    float align_ = 0;
    Marquee marquee_ = MarqueeFocused;
    // textW_ 是文字的步进宽度，逻辑像素。
    float textW_ = 0;
    Step step_ = HoldStart;
    float timer_ = 0;
    anim::Value scroll_;
};

// ---------------------------------------------------------------- Scroll

// Axes 选择滚动方向。
enum Axes : uint8_t
{
    ScrollVertical = 1 << 0,
    ScrollHorizontal = 1 << 1,
    ScrollBoth = ScrollVertical | ScrollHorizontal,
};

// kWaveSteps 是波浪从焦点往外最多再拖慢多少层。
constexpr int kWaveSteps = 8;

// Scroll 裁剪内容并通过动画移动内容的偏移来滚动：每个子节点一条自己的
// 弹簧，离焦点越远跟得越慢，于是内容像波浪一样追上，并在轴上稍微过冲。
// 方向键推动焦点穿过内容并把它滚进视野；越过最后一个可聚焦节点之后按步
// 滚动，到头时内容橡皮筋一样拉开、松手弹回。
class Scroll : public Node
{
  public:
    Scroll(Node *content, Axes axes = ScrollVertical, std::initializer_list<Option> opts = {});

    // setStagger 设置每远离焦点一步跟得慢多少，0 表示整体一起动。
    void setStagger(float f)
    {
        stagger_ = f;
    }
    // setScrollBar 显示或隐藏边上的滚动条。
    void setScrollBar(bool show)
    {
        hideBar_ = !show;
    }
    // content 是 add 挂子节点的地方。
    Node *content() const
    {
        return content_;
    }
    Vec2 scrollPosition() const
    {
        return Vec2{pos_[0], pos_[1]};
    }

    Node *contentHost() override
    {
        return content_;
    }
    int nodeTag() const override
    {
        return ui::kTagScroll;
    }
    bool isLayouter() const override
    {
        return true;
    }
    Vec2 measureChildren(Vec2 max) override;
    void arrangeChildren(Rect r) override;
    void reveal(Node *d) override;
    bool arrowScroll(KeyEvent &e, Node *next) override;
    void drawOver(gfx::Canvas &c) override;

  private:
    bool on(int i) const
    {
        return (axes_ & (i == 0 ? ScrollHorizontal : ScrollVertical)) != 0;
    }
    anim::Value *offset(int i)
    {
        return i == 0 ? content_->offsetX() : content_->offsetY();
    }
    Vec2 measureContent(Vec2 max);
    void apply();
    // wave 把内容的子节点留在它移动前的屏幕位置，让它们各自滑过去。
    void wave(int i, float d);
    // focusedChild 是持有焦点的子节点下标，没有时返回 -1。
    int focusedChild() const;
    // contentRest 是 d 在内容坐标系下静止时的矩形。
    Rect contentRest(Node *d) const;
    bool alongside(Node *d, int i) const;
    // outerRoom 报告外层滚动容器还能沿该方向走，能的话这个键就交给它。
    bool outerRoom(int i, float sign);
    // room 是内容沿该方向还能走多远。
    float room(int i, float sign) const;

    Node *content_ = nullptr;
    Axes axes_ = ScrollVertical;
    // pos 是滚动距离，起点为 0；pull 是按方向键顶出来的过卷，还没橡皮筋化。
    float pos_[2] = {0, 0};
    float pull_[2] = {0, 0};
    float limit_[2] = {0, 0};
    float view_[2] = {0, 0};
    // thumb 是显示出来的滚动距离，给滚动条用。
    anim::Value thumb_[2];

    float stagger_ = 0.2f;
    bool hideBar_ = false;
};

// -------------------------------------------------------------- ListItem

// ListItem 是可聚焦的一行，带一个标题和一个可选的尾部控件。在它上面松开
// OK 会运行 onClick；按住期间光标收缩，按下后变成拖动则什么也不做。带值
// 的行，例如 CheckBox 和 Select，在它之上搭建。
class ListItem : public Flex, public Control
{
  public:
    ListItem(const char *title = "", std::initializer_list<Option> opts = {});

    Label *title() const
    {
        return title_;
    }
    void setFont(const gfx::Font *f)
    {
        title_->setFont(f);
    }
    void setTrailing(Node *d);

    // activate 给有自己行为的行替换点击处理器。
    std::function<void()> activate;

    bool handleKey(KeyEvent &e) override;
    void update(float dt) override;
    void drawOver(gfx::Canvas &c) override;

  protected:
    void init(const char *title);

    Label *title_ = nullptr;
    Node *trailing_ = nullptr;
    bool waiting_ = false;
};

// ---------------------------------------------------------------- List

// List 是竖直滚动的列。add 会把行追加到列里。
class List : public Scroll
{
  public:
    List(std::initializer_list<Option> opts = {});

    // column 是放行的 Flex，可以调整间距或对齐。
    Flex *column() const
    {
        return column_;
    }

  private:
    Flex *column_ = nullptr;
};

// ---------------------------------------------------------------- Button

// Button 在它上面松开 OK 时运行 onClick。按住 OK 时光标从左往右把按钮吃
// 掉，吃完就触发。处理器失败时按钮抖一下并画一个叉。
class Button : public Node, public Control
{
  public:
    Button(const char *text = "", std::initializer_list<Option> opts = {});

    Label *label() const
    {
        return label_;
    }
    void setFont(const gfx::Font *f)
    {
        label_->setFont(f);
    }
    // setHold 要求按住 OK 这么多秒才触发，0 表示松开即触发。
    void setHold(float seconds)
    {
        hold_ = seconds;
    }
    // after 在处理器返回后运行。
    Done after;

    bool handleKey(KeyEvent &e) override;
    void update(float dt) override;
    Rect cursorShape(Rect r) override;
    void draw(gfx::Canvas &c) override;

  private:
    enum State : uint8_t
    {
        Idle = 0,
        Loading,
        Succeeded,
        Failed
    };

    void fire();
    void done(bool ok);

    Label *label_ = nullptr;
    State state_ = Idle;
    float timer_ = 0;
    // mark 是还留在屏幕上的结果；result 从 0 升到 1 表示它升上来，到 2 表示
    // 它向上离开。
    State mark_ = Idle;
    float hold_ = 0;
    bool holding_ = false;
    bool fired_ = false;
    anim::Value progress_;
    anim::Value result_;
};

// ----------------------------------------------------------------- Mark

// kMarkSize 是方框或圆圈的边长，逻辑像素。
constexpr float kMarkSize = 9;

// Mark 是 CheckBox 和 Radio 画的那个方框或圆圈；on 从 0 弹到 1。
class Mark : public Node
{
  public:
    explicit Mark(bool radio);

    bool isLeaf() const override
    {
        return true;
    }
    Vec2 measure(Vec2 max) override;
    void turn(bool on);
    // jump 立刻到目标状态，不给启动动画用。
    void jump(bool on);
    void draw(gfx::Canvas &c) override;

  private:
    bool radio_;
    anim::Value on_;
};

// ------------------------------------------------------------- CheckBox

// CheckBox 是松开 OK 时翻转方框的列表行。方框立刻翻；处理器返回 false 就
// 翻回去。
class CheckBox : public ListItem
{
  public:
    CheckBox(const char *title, bool checked, std::initializer_list<Option> opts = {});

    bool checked() const
    {
        return checked_;
    }
    // setChecked 改动方框但不调用处理器。
    void setChecked(bool v);
    // onCheck 在用户改动后运行，返回 false 会把旧值放回去。
    std::function<bool(bool from, bool to)> onCheck;

  private:
    void toggle();

    Mark *mark_ = nullptr;
    bool checked_ = false;
};

// ------------------------------------------------------------ RadioGroup

class Radio;

// RadioGroup 保持它的 Radio 中恰好一个被选中。
class RadioGroup
{
  public:
    explicit RadioGroup(int selected = -1) : selected_(selected)
    {
    }

    int selected() const
    {
        return selected_;
    }
    // count / radioAt 给自己搭组的控件访问行。
    int count() const
    {
        return count_;
    }
    Radio *radioAt(int i) const
    {
        return (i >= 0 && i < count_) ? radios_[i] : nullptr;
    }
    // select 选中第 i 项但不调用处理器。
    void select(int i);
    // onCheck 在选中项变化后运行，返回 false 会把旧的选中项放回去。
    std::function<bool(int from, int to)> onCheck;
    // picked 在用户选中之后运行，给 Select 这类自己搭组的控件用。
    std::function<void(int from, int to)> picked;

  private:
    friend class Radio;
    static constexpr int kMaxRadios = 16;

    Radio *radios_[kMaxRadios] = {};
    int count_ = 0;
    int selected_ = -1;
    bool busy_ = false;
};

// Radio 属于某个 RadioGroup 的列表行。它的 onClick 在组的处理器之后运行。
class Radio : public ListItem
{
  public:
    Radio(RadioGroup *group, const char *title, std::initializer_list<Option> opts = {});

    int index() const
    {
        return index_;
    }

  private:
    friend class RadioGroup;
    void choose();

    Mark *mark_ = nullptr;
    RadioGroup *group_ = nullptr;
    int index_ = 0;
};

// ---------------------------------------------------------------- Select

// Select 是显示当前选项的列表行；激活它会打开一个底部抽屉，里面是一列
// 单选。选中一项会关掉抽屉，选项变了才运行 onChange。
class Select : public ListItem
{
  public:
    Select(const char *title, const char *const *options, int count, int selected,
           std::initializer_list<Option> opts = {});

    int selected() const
    {
        return selected_;
    }
    // setSelected 显示第 i 项但不调用处理器。
    void setSelected(int i);
    // onChange 在用户改动后运行，返回 false 会把旧选项放回去。
    std::function<bool(int from, int to)> onChange;

  private:
    void open();
    const char *optionText() const;

    Label *value_ = nullptr;
    const char *const *options_ = nullptr;
    int count_ = 0;
    int selected_ = 0;
};

// ---------------------------------------------------------------- Drawer

// Edge 是抽屉从屏幕哪条边滑进来。
enum Edge : uint8_t
{
    Bottom = 0,
    Top,
    Left,
    Right
};

// Drawer 是从屏幕某条边滑进来的列，带一个可选的小标题。上下抽屉占满宽度、
// 高度按内容；左右抽屉占满高度、宽度按内容。Back 关闭抽屉。
class Drawer : public Flex
{
  public:
    Drawer(Edge edge, const char *title = "", std::initializer_list<Option> opts = {});

    Edge edge() const
    {
        return edge_;
    }
    // title 是内容上方的小标题，没有时为空。
    Label *title() const
    {
        return title_;
    }
    // open 把抽屉滑进来。
    void open();
    void close();
    // titleHeight 是标题占的高度，没有标题时是 0。
    float titleHeight() const;

  private:
    Edge edge_;
    Label *title_ = nullptr;
};

// ---------------------------------------------------------------- Dialog

// Dialog 是屏幕中央的框：标题、一行说明，以及一排平分宽度的按钮。按钮的
// 处理器不报错时对话框自动关闭。Back 也能关闭它。
class Dialog : public Flex
{
  public:
    Dialog(const char *title, const char *message, std::initializer_list<Button *> buttons = {});

    Label *title() const
    {
        return title_;
    }
    Label *message() const
    {
        return message_;
    }
    // open 显示对话框，焦点落在第一个按钮上。
    void open();
    void close();

  private:
    Label *title_ = nullptr;
    Label *message_ = nullptr;
    Flex *actions_ = nullptr;
};

// ---------------------------------------------------------------- Toast

// toast 在屏幕底部附近显示两秒文字。它既不抢焦点也不接按键，并且会替换
// 还在显示的上一个。
void toast(const char *text);

} // namespace widget
} // namespace lilty
