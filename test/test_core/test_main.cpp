// Desktop unit tests for the hardware-independent core.
//
// Everything here runs with `pio test -e native`: no Arduino, no board. The
// only thing the framework needs from a display is a gfx::Screen, so MockScreen
// below writes into a 1 KB bitmap and the whole pipeline runs unchanged.
//
// Run:  pio test -e native
#include <string.h>

#include <unity.h>

#include "lilty.h"

using namespace lilty;

namespace
{

// ------------------------------------------------------------ mock display

// MockScreen 是往内存写的 gfx::Screen。它和行为无关，只负责证明 core 除了这
// 五个纯虚函数之外什么都不需要。
class MockScreen : public gfx::Screen
{
  public:
    static constexpr int16_t kW = 128;
    static constexpr int16_t kH = 64;

    int16_t width() const override
    {
        return kW;
    }
    int16_t height() const override
    {
        return kH;
    }

    // 越界必须被忽略，不能断言也不能回绕——驱动是最后一道闸。
    void setPixel(int16_t x, int16_t y, const gfx::Color &c) override
    {
        if (x < 0 || y < 0 || x >= kW || y >= kH)
            return;
        uint8_t mask = uint8_t(1u << (y & 7));
        int i = int(y >> 3) * kW + x;
        if (gfx::isLit(c))
            bits_[i] |= mask;
        else
            bits_[i] &= uint8_t(~mask);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, const gfx::Color &c) override
    {
        for (int16_t j = 0; j < h; j++)
            for (int16_t i = 0; i < w; i++)
                setPixel(int16_t(x + i), int16_t(y + j), c);
    }

    bool canInvert() const override
    {
        return true;
    }

    void invertRect(int16_t x, int16_t y, int16_t w, int16_t h) override
    {
        for (int16_t j = 0; j < h; j++)
        {
            for (int16_t i = 0; i < w; i++)
            {
                int16_t px = int16_t(x + i), py = int16_t(y + j);
                if (px < 0 || py < 0 || px >= kW || py >= kH)
                    continue;
                bits_[int(py >> 3) * kW + px] ^= uint8_t(1u << (py & 7));
            }
        }
    }

    void flush() override
    {
        frames_++;
    }

    bool lit(int16_t x, int16_t y) const
    {
        if (x < 0 || y < 0 || x >= kW || y >= kH)
            return false;
        return (bits_[int(y >> 3) * kW + x] >> (y & 7)) & 1;
    }
    int litCount() const
    {
        int n = 0;
        for (int16_t y = 0; y < kH; y++)
            for (int16_t x = 0; x < kW; x++)
                if (lit(x, y))
                    n++;
        return n;
    }

    void clear()
    {
        memset(bits_, 0, sizeof bits_);
        frames_ = 0;
    }
    int frames() const
    {
        return frames_;
    }

  private:
    uint8_t bits_[kW * kH / 8] = {};
    int frames_ = 0;
};

MockScreen screen;

// settle 跑几帧：第一帧完成布局，后面几帧让入场弹簧收敛。
void settle(int n = 1)
{
    for (int i = 0; i < n; i++)
        ui::frame(1.0f / 60);
}

// press 投一次按下加松手，模拟旋钮转一格或按一下。
void press(ui::Key k)
{
    ui::emit(k, ui::Press);
    ui::emit(k, ui::Release);
    settle();
}

// ---------------------------------------------------------------- gfx

void test_rect_intersects()
{
    gfx::Rect a{0, 0, 10, 10};
    gfx::Rect b{5, 5, 10, 10};
    gfx::Rect i = a.intersect(b);
    TEST_ASSERT_EQUAL_INT16(5, i.x);
    TEST_ASSERT_EQUAL_INT16(5, i.y);
    TEST_ASSERT_EQUAL_INT16(5, i.w);
    TEST_ASSERT_EQUAL_INT16(5, i.h);
    TEST_ASSERT_TRUE(a.overlaps(b));
    TEST_ASSERT_FALSE(a.overlaps(gfx::Rect{20, 20, 4, 4}));
}

void test_mono_threshold()
{
    TEST_ASSERT_TRUE(gfx::isLit(gfx::White));
    TEST_ASSERT_FALSE(gfx::isLit(gfx::Black));
    // 中灰按亮度加权后算亮，单色屏上才不会把浅色底当成黑。
    TEST_ASSERT_TRUE(gfx::isLit(gfx::Grey));
}

// ---------------------------------------------------------------- 布局

void test_flex_stacks_vertically()
{
    ui::Flex *page = new ui::Flex(ui::Vertical);
    page->setPadding(ui::all(2));
    page->setGap(2);
    widget::Label *a = new widget::Label("A");
    widget::Label *b = new widget::Label("B");
    ui::add(page, a);
    ui::add(page, b);
    ui::boot(&screen, page);
    settle();

    TEST_ASSERT_TRUE(b->rect().y > a->rect().y);
    // 两行之间的空隙正好是 gap。
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, b->rect().y - (a->rect().y + a->rect().h));
}

void test_grow_takes_the_rest()
{
    ui::Flex *page = new ui::Flex(ui::Vertical);
    page->setPadding(ui::all(2));
    widget::Label *head = new widget::Label("head");
    widget::Flex *body = new widget::Flex(ui::Vertical);
    body->setGrow(1);
    ui::add(page, head);
    ui::add(page, body);
    ui::boot(&screen, page);
    settle();

    // body 吃掉标题之外的全部高度。
    TEST_ASSERT_FLOAT_WITHIN(0.01f, page->contentRect().h, head->rect().h + body->rect().h);
}

// ---------------------------------------------------------------- 焦点

void test_focus_starts_on_first_focusable()
{
    ui::Flex *page = new ui::Flex(ui::Vertical);
    widget::Button *a = new widget::Button("A");
    widget::Button *b = new widget::Button("B");
    ui::add(page, a);
    ui::add(page, b);
    ui::boot(&screen, page);
    settle();

    TEST_ASSERT_EQUAL_PTR(a, ui::focused());
}

void test_focus_moves_down_a_column()
{
    ui::Flex *page = new ui::Flex(ui::Vertical);
    widget::Button *a = new widget::Button("A");
    widget::Button *b = new widget::Button("B");
    ui::add(page, a);
    ui::add(page, b);
    ui::boot(&screen, page);
    settle();

    press(ui::Key::Down);
    TEST_ASSERT_EQUAL_PTR(b, ui::focused());
    press(ui::Key::Up);
    TEST_ASSERT_EQUAL_PTR(a, ui::focused());
}

// 这是回归测试：旋钮只有 Up/Down，横排的 OK/Cancel 必须也能切。导航在看到
// 横排容器时会把上下翻译成左右，转到底还会绕回来。
void test_focus_crosses_a_horizontal_row()
{
    ui::Flex *page = new ui::Flex(ui::Vertical);
    ui::Flex *row = new ui::Flex(ui::Horizontal);
    widget::Button *ok = new widget::Button("OK");
    widget::Button *cancel = new widget::Button("Cancel");
    ok->setGrow(1);
    cancel->setGrow(1);
    ui::add(row, ok);
    ui::add(row, cancel);
    ui::add(page, row);
    ui::boot(&screen, page);
    settle();

    TEST_ASSERT_EQUAL_PTR(ok, ui::focused());
    press(ui::Key::Down);
    TEST_ASSERT_EQUAL_PTR(cancel, ui::focused());
    press(ui::Key::Down);
    TEST_ASSERT_EQUAL_PTR(ok, ui::focused());
    press(ui::Key::Up);
    TEST_ASSERT_EQUAL_PTR(cancel, ui::focused());
}

void test_dialog_puts_focus_on_first_button()
{
    ui::Flex *home = new ui::Flex(ui::Vertical);
    ui::add(home, new widget::Button("Home"));
    ui::boot(&screen, home);
    settle();

    widget::Button *ok = new widget::Button("OK");
    widget::Button *cancel = new widget::Button("Cancel");
    widget::Dialog *d = new widget::Dialog("Confirm", "Delete 12 files?", {ok, cancel});
    d->open();
    settle();

    TEST_ASSERT_EQUAL_PTR(ok, ui::focused());
    press(ui::Key::Down);
    TEST_ASSERT_EQUAL_PTR(cancel, ui::focused());
}

// ---------------------------------------------------------------- 层栈

void test_push_and_pop_restore_focus()
{
    ui::Flex *home = new ui::Flex(ui::Vertical);
    widget::Button *a = new widget::Button("A");
    ui::add(home, a);
    ui::boot(&screen, home);
    settle();

    ui::Flex *next = new ui::Flex(ui::Vertical);
    widget::Button *b = new widget::Button("B");
    ui::add(next, b);
    ui::push(next);
    settle();

    TEST_ASSERT_EQUAL_PTR(b, ui::focused());

    ui::pop();
    // 退场是动画，层收紧到静止之后才真正回收，所以多跑几帧。
    settle(40);
    TEST_ASSERT_EQUAL_PTR(a, ui::focused());
}

void test_back_closes_the_top_layer()
{
    ui::Flex *home = new ui::Flex(ui::Vertical);
    ui::add(home, new widget::Button("Home"));
    ui::boot(&screen, home);
    settle();

    widget::Button *ok = new widget::Button("OK");
    widget::Dialog *d = new widget::Dialog("Confirm", "", {ok});
    d->open();
    settle();

    press(ui::Key::Back);
    settle(40);
    // 弹窗关掉后焦点回到下面那一层。
    ui::Node *first = home->firstChild();
    TEST_ASSERT_EQUAL_PTR(first, ui::focused());
}

// ---------------------------------------------------------------- 绘制

void test_frame_paints_something()
{
    ui::Flex *page = new ui::Flex(ui::Vertical);
    page->setPadding(ui::all(2));
    ui::add(page, new widget::Button("Hello"));
    ui::boot(&screen, page);
    screen.clear();
    settle(4);

    TEST_ASSERT_TRUE(screen.frames() > 0);
    TEST_ASSERT_TRUE(screen.litCount() > 0); // 至少画出按钮边框和文字
}

// 越界写入是驱动最容易崩的地方，这里直接怼到边界外面。
void test_out_of_bounds_pixel_is_ignored()
{
    screen.clear();
    screen.setPixel(-1, 0, gfx::White);
    screen.setPixel(0, -1, gfx::White);
    screen.setPixel(MockScreen::kW, 0, gfx::White);
    screen.setPixel(0, MockScreen::kH, gfx::White);
    TEST_ASSERT_EQUAL_INT(0, screen.litCount());
}

} // namespace

void setUp()
{
    ui::reset();
    screen.clear();
}

void tearDown()
{
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_rect_intersects);
    RUN_TEST(test_mono_threshold);
    RUN_TEST(test_flex_stacks_vertically);
    RUN_TEST(test_grow_takes_the_rest);
    RUN_TEST(test_focus_starts_on_first_focusable);
    RUN_TEST(test_focus_moves_down_a_column);
    RUN_TEST(test_focus_crosses_a_horizontal_row);
    RUN_TEST(test_dialog_puts_focus_on_first_button);
    RUN_TEST(test_push_and_pop_restore_focus);
    RUN_TEST(test_back_closes_the_top_layer);
    RUN_TEST(test_frame_paints_something);
    RUN_TEST(test_out_of_bounds_pixel_is_ignored);
    return UNITY_END();
}
