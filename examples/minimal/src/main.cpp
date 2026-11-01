// minimal: lilty 的最小可运行骨架。
//
// 一屏、一个列表、四个键。port 到新硬件时先把这一份跑起来：显示后端只要能
// 画 128×64，输入后端只要能发 Up / Down / OK / Back，界面这边一行都不用改。
// 完整 demo 见 examples/esp32s3-sh1106-rotary。
#include <Arduino.h>

#include "lilty.h"
#include "display/sh1106_i2c.h"
#include "input/rotary.h"

using lilty::ui::Node;
using lilty::widget::Label;
using lilty::widget::List;
using lilty::widget::ListItem;

// 引脚按你的板子改。
constexpr int kPinSda = 8;
constexpr int kPinScl = 9;
constexpr int kPinEncA = 14;
constexpr int kPinEncB = 15;
constexpr int kPinEncBtn = 16;
constexpr int kPinBack = 17;

static lilty::display::Sh1106I2C oled(0x3C);
static lilty::input::Rotary nav;

// makeHome 搭出一屏：顶上一行标题，下面是撑满的列表。顶层的 Flex 就是根页面，
// 由层栈推入，不需要自己管转场。
static Node *makeHome()
{
    List *list = new List();
    static const char *const kItems[] = {"One", "Two", "Three", "Four"};
    for (int i = 0; i < 4; i++)
    {
        ListItem *row = new ListItem(kItems[i]);
        row->onClick = []() {
            lilty::widget::toast("Tapped");
            return true;
        };
        lilty::ui::add(list, row);
    }

    lilty::ui::Flex *page = new lilty::ui::Flex(lilty::ui::Vertical);
    page->setPadding(lilty::ui::all(2));
    page->setGap(2);

    Label *head = new Label("lilty");
    head->setColor(lilty::ui::theme.accent);
    lilty::ui::add(page, head);

    list->setGrow(1); // 吃掉剩下的高度
    lilty::ui::add(page, list);
    return page;
}

void setup()
{
    Serial.begin(115200);
    // 没找到屏也继续跑，这样串口日志和界面逻辑仍然能验证。
    if (!oled.begin(kPinSda, kPinScl))
        Serial.println("SH1106 did not answer");
    nav.begin(kPinEncA, kPinEncB, kPinEncBtn, kPinBack);
    lilty::setFps(60);
    lilty::begin(&oled, makeHome());
}

// hook 比 ui::frame 先跑，所以这一帧采到的输入当帧就生效。
void loop()
{
    lilty::loop([](float dt) { nav.poll(dt); });
}
