// ESP32-S3 + 1.3 吋 SH1106(I2C) + 旋转编码器 demo。
//
// 这是 lilty 的参考移植：库就是在这套硬件上写出来的，所以它同时也当冒烟测试
// 用——布局、焦点导航、层栈改动之后先跑这一份。只想要最小骨架看
// examples/minimal。
//
// 四个页面演示 lilty 的主要手感：光标朝目标拉长再收拢、层栈的整屏推入和弹
// 窗、列表滚动时的波浪、按住确认。方向键除了移动焦点，还会把内容滚进视
// 野；到头时内容会被橡皮筋一样拉开。
//
// 接线（都可以在下面改）：
//   OLED  SDA -> GPIO8   SCL -> GPIO9   VCC 3V3   GND GND
//   编码器 A -> GPIO14   B -> GPIO15    SW -> GPIO16   公共端 GND
//   BACK  -> GPIO17  另一端 GND
// 这些脚都不占 USB(19/20)、JTAG(39~42) 和 PSRAM(26~32)。
#include <Arduino.h>
#include <stdio.h>

#include "lilty.h"
#include "input/rotary.h"
#include "display/sh1106_i2c.h"

constexpr int kPinSda = 8;
constexpr int kPinScl = 9;
constexpr int kPinEncA = 14;
constexpr int kPinEncB = 15;
constexpr int kPinEncBtn = 16;
constexpr int kPinBack = 17;
constexpr uint8_t kAddr = 0x3C;
// 模块背面那块跳线换个位置地址就是 0x3D，两个都试一遍省得改代码。
constexpr uint8_t kAddrAlt = 0x3D;

using lilty::ui::Node;
using lilty::widget::Button;
using lilty::widget::CheckBox;
using lilty::widget::Dialog;
using lilty::widget::Label;
using lilty::widget::List;
using lilty::widget::ListItem;
using lilty::widget::Select;

static lilty::display::Sh1106I2C oled(kAddr);
static lilty::input::Rotary nav;

static Node *makeControls();
static Node *makeLongList();

// logAction 在串口上留一行“谁被点了”。上板之后没有调试器，屏幕上不按预期
// 变化时，靠这行就能分清是按键没送到、还是回调没跑、还是画不出来。
static void logAction(const char *what)
{
    Serial.printf("[点击] %s\n", what);
}

// newPage 搭一屏：顶上一行强调色标题，下面是撑满的内容。整屏由层栈负责
// 从右边推进来，所以页面本身不用管转场。
static lilty::ui::Flex *newPage(const char *title, Node *body)
{
    lilty::ui::Flex *page = new lilty::ui::Flex(lilty::ui::Vertical);
    page->setPadding(lilty::ui::all(2));
    page->setGap(2);
    Label *head = new Label(title);
    head->setColor(lilty::ui::theme.accent);
    head->setPadding(lilty::ui::ltrb(2, 0, 2, 0));
    lilty::ui::attach(page, head);
    body->setGrow(1);
    lilty::ui::add(page, body);
    return page;
}

static void showDialog()
{
    Button *ok = new Button("OK");
    ok->onClick = []() {
        logAction("Dialog OK");
        lilty::widget::toast("Deleted 12 files");
        return true;
    };
    Button *cancel = new Button("Cancel");
    cancel->onClick = []() {
        logAction("Dialog Cancel");
        return true;
    };
    Dialog *d = new Dialog("Confirm", "Delete 12 files?", {ok, cancel});
    d->open();
}

static Node *makeHome()
{
    List *list = new List();

    ListItem *controls = new ListItem("Controls");
    controls->onClick = []() {
        logAction("Controls");
        lilty::ui::push(makeControls());
        return true;
    };
    lilty::ui::add(list, controls);

    ListItem *rows = new ListItem("Long list");
    rows->onClick = []() {
        logAction("Long list");
        lilty::ui::push(makeLongList());
        return true;
    };
    lilty::ui::add(list, rows);

    ListItem *dlg = new ListItem("Dialog");
    dlg->onClick = []() {
        logAction("Dialog");
        showDialog();
        return true;
    };
    lilty::ui::add(list, dlg);

    ListItem *tst = new ListItem("Toast");
    tst->onClick = []() {
        logAction("Toast");
        lilty::widget::toast("Saved");
        return true;
    };
    lilty::ui::add(list, tst);

    return newPage("lilty", list);
}

static Node *makeControls()
{
    List *list = new List();

    CheckBox *sound = new CheckBox("Sound", true);
    sound->onCheck = [](bool, bool to) {
        logAction(to ? "Sound 开" : "Sound 关");
        return true;
    };
    lilty::ui::add(list, sound);

    static const char *const kThemes[] = {"Midnight", "Paper", "Terminal"};
    Select *themeSel = new Select("Theme", kThemes, 3, 0);
    themeSel->onChange = [](int, int to) {
        Serial.printf("[点击] Theme -> %s\n", kThemes[to]);
        return true;
    };
    lilty::ui::add(list, themeSel);

    Button *hold = new Button("Hold 0.8s");
    hold->setHold(0.8f);
    hold->onClick = []() {
        logAction("Hold 确认");
        lilty::widget::toast("Confirmed");
        return true;
    };
    lilty::ui::add(list, hold);

    Button *fail = new Button("Fail");
    fail->onClick = []() {
        logAction("Fail（故意失败）");
        return false;
    };
    lilty::ui::add(list, fail);

    return newPage("Controls", list);
}

static Node *makeLongList()
{
    List *list = new List();
    char buf[24];
    for (int i = 1; i <= 40; i++)
    {
        snprintf(buf, sizeof buf, "Row %02d", i);
        ListItem *row = new ListItem(buf);
        row->onClick = []() {
            logAction("行");
            lilty::widget::toast("Tapped");
            return true;
        };
        lilty::ui::add(list, row);
    }
    return newPage("Long list", list);
}

// onFrame 先采按键再让框架推进一帧，顺便每两秒报一次帧率和堆余量——
// 上板之后没有调试器，这两个数字是判断“卡住”还是“在跑”最快的依据。
static void onFrame(float dt)
{
    nav.poll(dt);
    static float acc = 0;
    static uint32_t frames = 0;
    acc += dt;
    frames++;
    if (acc < 2.0f)
        return;
    Serial.printf("运行中：%.0f fps，堆余 %u 字节\n", float(frames) / acc, unsigned(ESP.getFreeHeap()));
    acc = 0;
    frames = 0;
}

// scanBus 把总线上有应答的地址打出来。屏不亮时先看这个：一个都没有就是
// 接线或供电的问题，有 0x3C 却不应答一个整屏，那才是驱动参数的问题。
static void scanBus()
{
    Serial.println("扫描 I2C 总线：");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++)
    {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() != 0)
            continue;
        Serial.printf("  0x%02X 有应答\n", a);
        found++;
    }
    if (found == 0)
        Serial.println("  空：测 SDA/SCL 有没有接反，VCC 是不是 3V3");
}

static bool startOled()
{
    if (oled.begin(kPinSda, kPinScl))
    {
        Serial.printf("SH1106 就绪：0x%02X，SDA=%d SCL=%d\n", kAddr, kPinSda, kPinScl);
        return true;
    }
    oled.setAddress(kAddrAlt);
    if (oled.begin(kPinSda, kPinScl))
    {
        Serial.printf("SH1106 就绪：0x%02X，SDA=%d SCL=%d\n", kAddrAlt, kPinSda, kPinScl);
        return true;
    }
    Serial.println("SH1106 在 0x3C / 0x3D 都没有应答");
    scanBus();
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(50);
    // 没找到屏也继续跑，这样串口日志和界面逻辑仍然能验证。
    startOled();
    nav.begin(kPinEncA, kPinEncB, kPinEncBtn, kPinBack);
    lilty::setFps(60);
    lilty::begin(&oled, makeHome());
}

void loop()
{
    lilty::loop(&onFrame);
}
