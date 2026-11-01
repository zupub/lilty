#include "core/gfx.h"

namespace lilty
{
namespace gfx
{

const Glyph *fontGlyph(const Font *f, char c)
{
    if (!f)
        return nullptr;
    uint8_t i = uint8_t(uint8_t(c) - 32);
    if (uint8_t(c) < 32 || i >= f->count)
        return nullptr;
    return &f->glyphs[i];
}

int16_t textWidth(const Font *f, const char *s)
{
    if (!f)
        return 0;
    int16_t w = 0;
    for (; s && *s; s++)
    {
        const Glyph *g = fontGlyph(f, *s);
        w = int16_t(w + (g ? g->advance : int8_t(f->fallbackAdvance)));
    }
    return w;
}

void Canvas::strokeRect(const Rect &r, const Color &c)
{
    if (r.empty())
        return;
    hline(r.x, r.y, r.w, c);
    hline(r.x, int16_t(r.bottom() - 1), r.w, c);
    vline(r.x, int16_t(r.y + 1), int16_t(r.h - 2), c);
    vline(int16_t(r.right() - 1), int16_t(r.y + 1), int16_t(r.h - 2), c);
}

void Canvas::fillRoundRect(const Rect &r, int16_t radius, const Color &c)
{
    roundSpans(r, radius, c, false);
}

void Canvas::invertRoundRect(const Rect &r, int16_t radius)
{
    if (!inv_)
        return;
    roundSpans(r, radius, Color{}, true);
}

// roundSpans 用标志位而不是回调，省掉逐帧绘制光标时的闭包分配。
void Canvas::roundSpans(const Rect &r, int16_t radius, const Color &c, bool invert)
{
    if (r.empty())
        return;
    radius = minI(radius, minI(int16_t(r.w / 2), int16_t(r.h / 2)));
    if (radius <= 0)
    {
        span(r, c, invert);
        return;
    }
    Rect body{r.x, int16_t(r.y + radius), r.w, int16_t(r.h - 2 * radius)};
    if (!body.empty())
        span(body, c, invert);
    for (int16_t i = 0; i < radius; i++)
    {
        int16_t in = cornerInset(radius, i);
        int16_t w = int16_t(r.w - 2 * in);
        span(Rect{int16_t(r.x + in), int16_t(r.y + i), w, 1}, c, invert);
        span(Rect{int16_t(r.x + in), int16_t(r.bottom() - 1 - i), w, 1}, c, invert);
    }
}

void Canvas::span(const Rect &r, const Color &c, bool invert)
{
    if (invert)
        invertRect(r);
    else
        fillRect(r, c);
}

void Canvas::strokeRoundRect(const Rect &r, int16_t radius, const Color &c)
{
    if (r.empty())
        return;
    radius = minI(radius, minI(int16_t(r.w / 2), int16_t(r.h / 2)));
    if (radius <= 0)
    {
        strokeRect(r, c);
        return;
    }
    hline(int16_t(r.x + radius), r.y, int16_t(r.w - 2 * radius), c);
    hline(int16_t(r.x + radius), int16_t(r.bottom() - 1), int16_t(r.w - 2 * radius), c);
    vline(r.x, int16_t(r.y + radius), int16_t(r.h - 2 * radius), c);
    vline(int16_t(r.right() - 1), int16_t(r.y + radius), int16_t(r.h - 2 * radius), c);
    int16_t prev = radius;
    for (int16_t i = 0; i < radius; i++)
    {
        int16_t in = cornerInset(radius, i);
        // 与上一行的内缩连起来，圆弧陡峭的部分才不会断开。
        int16_t w = maxI(int16_t(prev - in), 1);
        if (i == 0)
            w = int16_t(radius - in);
        int16_t yt = int16_t(r.y + i), yb = int16_t(r.bottom() - 1 - i);
        hline(int16_t(r.x + in), yt, w, c);
        hline(int16_t(r.right() - in - w), yt, w, c);
        hline(int16_t(r.x + in), yb, w, c);
        hline(int16_t(r.right() - in - w), yb, w, c);
        prev = in;
    }
}

// cornerInset 是半径 r 的圆角第 i 行内缩多少。加 r 后开方相当于四舍五入，
// 小半径才不会看起来是方的。
int16_t cornerInset(int16_t r, int16_t i)
{
    int32_t dy = int32_t(r - i);
    int32_t rr = int32_t(r);
    int32_t dx = isqrt(rr * rr - dy * dy + rr);
    return int16_t(r - int16_t(dx));
}

int32_t isqrt(int32_t n)
{
    if (n <= 0)
        return 0;
    int32_t x = n, y = (x + 1) / 2;
    while (y < x)
    {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

void Canvas::fillCircle(int16_t cx, int16_t cy, int16_t r, const Color &c)
{
    fillRoundRect(Rect{int16_t(cx - r), int16_t(cy - r), int16_t(2 * r + 1), int16_t(2 * r + 1)}, int16_t(r + 1), c);
}

void Canvas::strokeCircle(int16_t cx, int16_t cy, int16_t r, const Color &c)
{
    strokeRoundRect(Rect{int16_t(cx - r), int16_t(cy - r), int16_t(2 * r + 1), int16_t(2 * r + 1)}, int16_t(r + 1), c);
}

void Canvas::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const Color &c)
{
    int16_t dx = int16_t(x1 - x0), dy = int16_t(y1 - y0);
    int16_t sx = 1, sy = 1;
    if (dx < 0)
    {
        dx = int16_t(-dx);
        sx = -1;
    }
    if (dy < 0)
    {
        dy = int16_t(-dy);
        sy = -1;
    }
    int16_t err = int16_t(dx - dy);
    for (;;)
    {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            return;
        int16_t e2 = int16_t(2 * err);
        if (e2 > -dy)
        {
            err = int16_t(err - dy);
            x0 = int16_t(x0 + sx);
        }
        if (e2 < dx)
        {
            err = int16_t(err + dx);
            y0 = int16_t(y0 + sy);
        }
    }
}

int16_t Canvas::text(const Font *f, int16_t x, int16_t baseline, const char *s, const Color &c)
{
    if (!f)
        return x;
    int16_t top = int16_t(baseline - f->ascent);
    for (; s && *s; s++)
    {
        const Glyph *g = fontGlyph(f, *s);
        if (!g)
        {
            x = int16_t(x + f->fallbackAdvance);
            continue;
        }
        int16_t gx = int16_t(x + g->xOffset);
        int16_t gy = int16_t(top + g->yOffset);
        if (visible(Rect{gx, gy, g->w, g->h}))
        {
            for (uint8_t j = 0; j < g->h && j < 7; j++)
            {
                const char *row = g->rows[j];
                for (uint8_t i = 0; i < g->w; i++)
                {
                    if (row[i] == '#')
                        pixel(int16_t(gx + i), int16_t(gy + j), c);
                }
            }
        }
        x = int16_t(x + g->advance);
    }
    return x;
}

} // namespace gfx
} // namespace lilty
