// gfx: 绘制层。Screen 是显示驱动要实现的接口，Canvas 在其上叠加
// 裁剪、平移和形状绘制。对应 lilt 的 gfx 包。
#pragma once

#include <stdint.h>

namespace lilty
{
namespace gfx
{

// Color 用普通 RGBA，彩屏和单色屏共用一套 API；单色驱动用 isLit 决定
// 一个像素是否点亮。
struct Color
{
    uint8_t r, g, b, a;
};

constexpr Color Black{0, 0, 0, 255};
constexpr Color White{255, 255, 255, 255};
constexpr Color Grey{128, 128, 128, 255};

inline Color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return Color{r, g, b, 255};
}

// isLit 判断 c 在单色屏上是否点亮。
inline bool isLit(const Color &c)
{
    return int(c.r) * 3 + int(c.g) * 6 + int(c.b) >= 128 * 10;
}

inline int16_t minI(int16_t a, int16_t b)
{
    return a < b ? a : b;
}
inline int16_t maxI(int16_t a, int16_t b)
{
    return a > b ? a : b;
}

// Rect 是像素矩形。
struct Rect
{
    int16_t x, y, w, h;

    constexpr bool empty() const
    {
        return w <= 0 || h <= 0;
    }
    constexpr int16_t right() const
    {
        return int16_t(x + w);
    }
    constexpr int16_t bottom() const
    {
        return int16_t(y + h);
    }

    constexpr bool contains(int16_t px, int16_t py) const
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    constexpr Rect offset(int16_t dx, int16_t dy) const
    {
        return Rect{int16_t(x + dx), int16_t(y + dy), w, h};
    }
    constexpr Rect inset(int16_t d) const
    {
        return Rect{int16_t(x + d), int16_t(y + d), int16_t(w - 2 * d), int16_t(h - 2 * d)};
    }

    Rect intersect(const Rect &o) const
    {
        int16_t x0 = maxI(x, o.x), y0 = maxI(y, o.y);
        int16_t x1 = minI(right(), o.right()), y1 = minI(bottom(), o.bottom());
        if (x1 <= x0 || y1 <= y0)
            return Rect{0, 0, 0, 0};
        return Rect{x0, y0, int16_t(x1 - x0), int16_t(y1 - y0)};
    }
    bool overlaps(const Rect &o) const
    {
        return !intersect(o).empty();
    }
};

// Screen 是显示驱动必须提供的接口。屏幕外的坐标必须被忽略而不是被拒绝。
class Screen
{
  public:
    virtual ~Screen()
    {
    }
    virtual int16_t width() const = 0;
    virtual int16_t height() const = 0;
    virtual void setPixel(int16_t x, int16_t y, const Color &c) = 0;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, const Color &c) = 0;
    // 能反色的屏实现这两个，光标会改成反色绘制；否则画成主题 Accent 色的描边。
    virtual bool canInvert() const
    {
        return false;
    }
    virtual void invertRect(int16_t x, int16_t y, int16_t w, int16_t h)
    {
        (void)x;
        (void)y;
        (void)w;
        (void)h;
    }
    // flush 把画好的一帧推给面板。
    virtual void flush() = 0;
};

// Glyph 是一个字形：rows 指向 h 行、每行 w 个字符的点阵，'#' 为亮点。
// 用字符画描述字形，源码可读、数据直接留在 flash 里，不占 RAM。
struct Glyph
{
    const char *rows[7];
    uint8_t w, h;
    int8_t advance;
    int8_t xOffset, yOffset;
};

// Font 是一套位图字体。glyphs 覆盖 ASCII 32 起的 count 个字符。
struct Font
{
    const Glyph *glyphs;
    uint8_t count;
    uint8_t height;
    uint8_t ascent;
    uint8_t lineHeight;
    uint8_t fallbackAdvance;
};

// font5x7 是内置的 5x7 ASCII 字体，字宽 6 像素（含 1 像素间距）。
extern const Font font5x7;

// fontGlyph 返回 c 的字形，没有时返回 nullptr。
const Glyph *fontGlyph(const Font *f, char c);
// textWidth 是 s 在 f 下的步进宽度。
int16_t textWidth(const Font *f, const char *s);
// ascent / lineHeight 是字体度量的快捷读取。
inline int16_t ascent(const Font *f)
{
    return f ? f->ascent : 0;
}
inline int16_t lineHeight(const Font *f)
{
    return f ? f->lineHeight : 0;
}

// Canvas 是 Screen 上一个已平移、已裁剪的视图。它是小值对象：translate 和
// clip 返回新画布，嵌套绘制不需要恢复状态。
class Canvas
{
  public:
    explicit Canvas(Screen *s)
        : s_(s), inv_(s && s->canInvert()),
          clip_(Rect{0, 0, int16_t(s ? s->width() : 0), int16_t(s ? s->height() : 0)}), ox_(0), oy_(0)
    {
    }

    Canvas translate(int16_t dx, int16_t dy) const
    {
        Canvas c = *this;
        c.ox_ = int16_t(c.ox_ + dx);
        c.oy_ = int16_t(c.oy_ + dy);
        return c;
    }

    // clip 把绘制限制在 r（局部坐标）内。
    Canvas clip(const Rect &r) const
    {
        Canvas c = *this;
        c.clip_ = c.clip_.intersect(r.offset(c.ox_, c.oy_));
        return c;
    }

    Canvas absolute() const
    {
        Canvas c = *this;
        c.ox_ = c.oy_ = 0;
        return c;
    }

    int16_t originX() const
    {
        return ox_;
    }
    int16_t originY() const
    {
        return oy_;
    }

    // bounds 返回局部坐标下的裁剪区。
    Rect bounds() const
    {
        return clip_.offset(int16_t(-ox_), int16_t(-oy_));
    }

    // visible 报告 r（局部坐标）是否还有任何部分可见。
    bool visible(const Rect &r) const
    {
        return clip_.overlaps(r.offset(ox_, oy_));
    }

    bool canInvert() const
    {
        return inv_;
    }

    void pixel(int16_t x, int16_t y, const Color &c)
    {
        x = int16_t(x + ox_);
        y = int16_t(y + oy_);
        if (clip_.contains(x, y))
            s_->setPixel(x, y, c);
    }

    void fillRect(const Rect &r, const Color &c)
    {
        Rect q = clip_.intersect(r.offset(ox_, oy_));
        if (!q.empty())
            s_->fillRect(q.x, q.y, q.w, q.h, c);
    }

    void invertRect(const Rect &r)
    {
        if (!inv_)
            return;
        Rect q = clip_.intersect(r.offset(ox_, oy_));
        if (!q.empty())
            s_->invertRect(q.x, q.y, q.w, q.h);
    }

    void hline(int16_t x, int16_t y, int16_t w, const Color &c)
    {
        fillRect(Rect{x, y, w, 1}, c);
    }
    void vline(int16_t x, int16_t y, int16_t h, const Color &c)
    {
        fillRect(Rect{x, y, 1, h}, c);
    }

    void strokeRect(const Rect &r, const Color &c);
    // 逐行画填充，填充和反色都只需每行一次调用。
    void fillRoundRect(const Rect &r, int16_t radius, const Color &c);
    void invertRoundRect(const Rect &r, int16_t radius);
    void strokeRoundRect(const Rect &r, int16_t radius, const Color &c);
    void fillCircle(int16_t cx, int16_t cy, int16_t r, const Color &c);
    void strokeCircle(int16_t cx, int16_t cy, int16_t r, const Color &c);
    // line 用 Bresenham，够画对勾、叉这类图标。
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const Color &c);

    // text 以 baseline 为基线画 s，返回最后一个字之后的 x。
    int16_t text(const Font *f, int16_t x, int16_t baseline, const char *s, const Color &c);

  private:
    void span(const Rect &r, const Color &c, bool invert);
    void roundSpans(const Rect &r, int16_t radius, const Color &c, bool invert);

    Screen *s_;
    bool inv_;
    Rect clip_;
    int16_t ox_, oy_;
};

// cornerInset 是半径 r 的圆角第 i 行内缩多少。
int16_t cornerInset(int16_t r, int16_t i);
int32_t isqrt(int32_t n);

} // namespace gfx
} // namespace lilty
