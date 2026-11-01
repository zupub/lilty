// ESP32-S3 + 1.3 吋 SH1106(I2C) + 旋转编码器 demo。
//
// 这是 lilty 的参考移植：库就是在这套硬件上写出来的，所以它同时也当冒烟测试
// 用——布局、焦点导航、层栈改动之后先跑这一份。只想要最小骨架看
// examples/minimal。
//
// 八个页面把现有的控件走一遍：按钮（普通 / 长按 / 故意失败）、复选框与单选、
// 下拉选择、Grid 数字键盘、横向滚动的卡片架、Flex 对齐、长列表，以及四向抽
// 屉、对话框和 toast。结构照搬原版 lilt 的 examples/gallery，凡原版用到 Input
// 或 Chips 的地方（lilty 没有这两个控件）都换成了别的演示。
//
// 方向键移动焦点，OK 激活，BACK 返回上一页。Grid 和横排卡片这两页要左右键才
// 走得完一趟，所以它们各自挂了一个 LinearNav，把上下两格改写成链上的前一格
// 和后一格。
//
// 接线（都可以在下面改）：
//   OLED  SDA -> GPIO8   SCL -> GPIO9   VCC 3V3   GND GND
//   编码器 A -> GPIO14   B -> GPIO15    SW -> GPIO16   公共端 GND
//   BACK  -> GPIO17  另一端 GND
// 这些脚都不占 USB(19/20)、JTAG(39~42) 和 PSRAM(26~32)。
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

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

using lilty::ui::KeyEvent;
using lilty::ui::Node;
using lilty::widget::Button;
using lilty::widget::CheckBox;
using lilty::widget::Dialog;
using lilty::widget::Drawer;
using lilty::widget::Label;
using lilty::widget::List;
using lilty::widget::ListItem;
using lilty::widget::Radio;
using lilty::widget::RadioGroup;
using lilty::widget::Scroll;
using lilty::widget::Select;

static lilty::display::Sh1106I2C oled(kAddr);
static lilty::input::Rotary nav;

static Node *makeButtons();
static Node *makeToggles();
static Node *makeSelects();
static Node *makeKeypad();
static Node *makeShelf();
static Node *makeFlexDemo();
static Node *makeLongList();
static Node *makePopups();

// logAction 在串口上留一行“谁被点了”。上板之后没有调试器，屏幕上不按预期
// 变化时，靠这行就能分清是按键没送到、还是回调没跑、还是画不出来。
static void logAction(const char *what) { Serial.printf("[点击] %s\n", what); }

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

// ---------------------------------------------------------------- 焦点链

// Keypad 的键盘是 3×4 的格子，Shelf 是两排横着铺开的卡片：两者都要靠左右
// 键才走得完一趟，而旋钮只给“上一格”和“下一格”。LinearNav 把子树里的可
// 聚焦节点按 add 的顺序串成一条链，转一格走一个，于是 Grid 和横排都退化成
// 一维罗列——1 之后就是 2，Recent 排走完直接接上 Favourites。继承 List 是
// 为了内容比屏幕高时还能滚。
class LinearNav : public List
{
  public:
    // previewKey 在按键冒泡到框架之前先看到它，所以改写 Up/Down 不会跟框
    // 架自己的空间导航打架。
    bool previewKey(KeyEvent &e) override
    {
        if (e.key != lilty::ui::Key::Up && e.key != lilty::ui::Key::Down)
            return false;
        // 按下只吞掉，移动发生在松手或按住连发时，和别的键保持同一节奏。
        if (!e.fires())
            return true;

        Node *chain[kChainCap];
        int n = collect(this, chain, 0);
        int at = -1;
        for (int i = 0; i < n; i++)
        {
            if (chain[i] == lilty::ui::focused())
                at = i;
        }
        if (at < 0)
            return false;
        int to = at + (e.key == lilty::ui::Key::Down ? 1 : -1);
        // 到两头就停住：旋钮转到底焦点不该绕回另一端。
        if (to < 0 || to >= n)
            return true;
        // 焦点落在链头/链尾时把整页拨到顶/底。reveal 只保证焦点那个节点
        // 看得见，它上方的标题和提示仍旧留在屏幕外。
        if (to == 0)
            scrollTo(0, 0);
        else if (to == n - 1)
            scrollTo(0, kBottom);
        lilty::ui::setFocus(chain[to]);
        return true;
    }

  private:
    static constexpr int kChainCap = 24;
    // kBottom 是个比任何内容都大的位置，交给 scrollTo 夹到底部。
    static constexpr float kBottom = 1e6f;

    // collect 按声明顺序把 d 子树里的可聚焦节点收进 out。
    static int collect(Node *d, Node **out, int n)
    {
        if (!d || n >= kChainCap)
            return n;
        if (d->focusable())
        {
            out[n++] = d;
            return n;
        }
        for (Node *c = d->firstChild(); c; c = c->nextSibling())
            n = collect(c, out, n);
        return n;
    }
};

static void showDialog()
{
    Button *ok = new Button("OK");
    ok->onClick = []()
    {
        logAction("Dialog OK");
        lilty::widget::toast("Deleted 12 files");
        return true;
    };
    Button *cancel = new Button("Cancel");
    cancel->onClick = []()
    {
        logAction("Dialog Cancel");
        return true;
    };
    Dialog *d = new Dialog("Confirm", "Delete 12 files?", {ok, cancel});
    d->open();
}

// --------------------------------------------------------------- Buttons

static Node *makeButtons()
{
    List *list = new List();

    // 按钮能改自己的标题：按一次数字加一。
    static int taps = 0;
    Button *tap = new Button("Tap me");
    tap->onClick = [tap]()
    {
        char buf[24];
        snprintf(buf, sizeof buf, "Tapped %d", ++taps);
        tap->label()->setText(buf);
        return true;
    };
    lilty::ui::add(list, tap);

    // 按住 OK 满 0.8 秒才触发，光标从左往右把按钮吃掉。
    Button *hold = new Button("Hold 0.8s");
    hold->setHold(0.8f);
    hold->onClick = []()
    {
        logAction("Hold 确认");
        lilty::widget::toast("Confirmed");
        return true;
    };
    lilty::ui::add(list, hold);

    // 处理器返回 false：按钮抖一下、画个叉，别的什么都不会发生。
    Button *fail = new Button("Fail on purpose");
    fail->onClick = []()
    {
        logAction("Fail（故意失败）");
        return false;
    };
    lilty::ui::add(list, fail);

    return newPage("Buttons", list);
}

// --------------------------------------------------------------- Toggles

static Node *makeToggles()
{
    List *list = new List();

    CheckBox *wifi = new CheckBox("Wi-Fi", false);
    wifi->onCheck = [](bool, bool to)
    {
        logAction(to ? "Wi-Fi 开" : "Wi-Fi 关");
        return true;
    };
    lilty::ui::add(list, wifi);

    CheckBox *bt = new CheckBox("Bluetooth", true);
    bt->onCheck = [](bool, bool to)
    {
        logAction(to ? "Bluetooth 开" : "Bluetooth 关");
        return true;
    };
    lilty::ui::add(list, bt);

    // 处理器返回 false：方框先翻过去，再抖一下翻回来。
    CheckBox *cloud = new CheckBox("Cloud sync (fails)", false);
    cloud->onCheck = [](bool, bool)
    {
        logAction("Cloud sync 被拒绝");
        return false;
    };
    lilty::ui::add(list, cloud);

    // 一组单选：任何时刻恰好选中一个。
    static const char *const kModes[] = {"Dark", "Light", "Auto"};
    RadioGroup *group = new RadioGroup(0);
    group->onCheck = [](int, int to)
    {
        logAction(kModes[to]);
        return true;
    };
    for (int i = 0; i < 3; i++)
    {
        lilty::ui::add(list, new Radio(group, kModes[i]));
    }

    return newPage("Toggles", list);
}

// ---------------------------------------------------------------- Select

static Node *makeSelects()
{
    List *list = new List();

    static const char *const kSpeeds[] = {"Slow", "Normal", "Fast", "Ludicrous"};
    Select *speed = new Select("Speed", kSpeeds, 4, 1);
    speed->onChange = [](int, int to)
    {
        Serial.printf("[点击] Speed -> %s\n", kSpeeds[to]);
        return true;
    };
    lilty::ui::add(list, speed);

    // 选项比屏幕一次能放下的多，抽屉里照样滚得动。
    static const char *const kLangs[] = {"English", "Deutsch", "Espanol", "Francais",
                                         "Italiano", "Nederlands", "Polski",
                                         "Portugues", "Svenska"};
    Select *lang = new Select("Language", kLangs, 9, 0);
    lang->onChange = [](int, int to)
    {
        Serial.printf("[点击] Language -> %s\n", kLangs[to]);
        return true;
    };
    lilty::ui::add(list, lang);

    return newPage("Select", list);
}

// ---------------------------------------------------------------- Keypad

static char gPin[9] = "";
static Label *gPinLabel = nullptr;

static void pressPin(const char *key)
{
    size_t n = strlen(gPin);
    if (key[0] == 'C')
    {
        n = 0;
    }
    else if (key[0] == '<')
    {
        if (n > 0)
            n--;
    }
    else if (n < sizeof gPin - 1)
    {
        gPin[n++] = key[0];
    }
    gPin[n] = 0;
    if (gPinLabel)
        gPinLabel->setText(n ? gPin : "Enter a PIN");
}

static Node *makeKeypad()
{
    // 四行键盘加一行输入提示，比 64px 屏的内容区高一点，用 LinearNav 兜住
    // 滚动；十二个键同时被串成一条链，旋钮从 1 一路转到 <。
    LinearNav *nav = new LinearNav();

    gPin[0] = 0;
    gPinLabel = new Label("Enter a PIN");
    gPinLabel->setAlign(0.5f);
    lilty::ui::add(nav, gPinLabel);

    // 十二个键平均分成三列，列宽由 Grid 的 fr 轨道平分。
    lilty::ui::Grid *pad = new lilty::ui::Grid(
        {lilty::ui::columns({lilty::ui::frTrack(1), lilty::ui::frTrack(1), lilty::ui::frTrack(1)}),
         lilty::ui::gap(2)});
    static const char *const kKeys[] = {"1", "2", "3", "4", "5", "6",
                                        "7", "8", "9", "C", "0", "<"};
    for (int i = 0; i < 12; i++)
    {
        Button *key = new Button(kKeys[i]);
        key->onClick = [i]()
        {
            pressPin(kKeys[i]);
            return true;
        };
        lilty::ui::add(pad, key);
    }
    lilty::ui::add(nav, pad);

    return newPage("Keypad", nav);
}

// ----------------------------------------------------------------- Shelf

static Label *gPlaying = nullptr;

// shelfRow 是一排横向铺开的按钮，包在只裁剪宽度的 Scroll 里：上下选排，
// 左右在排内移动，中间那几张会被光标带得稍微错开。
static Node *shelfRow(const char *const *names, int count)
{
    lilty::ui::Flex *row = new lilty::ui::Flex(lilty::ui::Horizontal);
    row->setGap(3);
    row->setPadding(lilty::ui::xy(2, 1));
    for (int i = 0; i < count; i++)
    {
        Button *card = new Button(names[i]);
        card->onClick = [names, i]()
        {
            char buf[40];
            snprintf(buf, sizeof buf, "Playing %s", names[i]);
            gPlaying->setText(buf);
            return true;
        };
        lilty::ui::add(row, card);
    }
    // 底部留 3px，免得滚动条压在光标上。
    return new Scroll(row, lilty::widget::ScrollHorizontal,
                      {lilty::ui::fillWidth(), lilty::ui::padding(lilty::ui::ltrb(0, 0, 0, 3))});
}

static Node *makeShelf()
{
    // 两排卡片接成一条链：Recent 六张走完接 Favourites 五张，旋钮不会在排
    // 尾绕回排首，也不会卡在排里出不来。同样用 LinearNav 兜住滚动。
    LinearNav *nav = new LinearNav();

    gPlaying = new Label("Pick a map");
    gPlaying->setPadding(lilty::ui::xy(2, 0));
    lilty::ui::add(nav, gPlaying);

    static const char *const kRecent[] = {"Blue Zenith", "Freedom Dive", "Teo",
                                          "Glory Days", "Harumachi Clover", "Senbonzakura"};
    static const char *const kFavourites[] = {"Kani Do-Raku", "Snow Drive", "Big Black",
                                              "Night of Knights", "Airman"};

    Label *l1 = new Label("Recent");
    l1->setColor(lilty::ui::theme.muted);
    lilty::ui::add(nav, l1);
    lilty::ui::add(nav, shelfRow(kRecent, 6));

    Label *l2 = new Label("Favourites");
    l2->setColor(lilty::ui::theme.muted);
    lilty::ui::add(nav, l2);
    lilty::ui::add(nav, shelfRow(kFavourites, 5));

    return newPage("Shelf", nav);
}

// ------------------------------------------------------------------ Flex

static Node *makeFlexDemo()
{
    lilty::ui::Flex *row = new lilty::ui::Flex(lilty::ui::Horizontal);
    row->setGap(2);
    row->setPadding(lilty::ui::xy(4, 2));

    static const char *const kBoxes[] = {"1", "2", "3"};
    Label *middle = nullptr;
    for (int i = 0; i < 3; i++)
    {
        Label *box = new Label(kBoxes[i], {lilty::ui::border(lilty::ui::theme.foreground),
                                           lilty::ui::radius(lilty::ui::theme.radius),
                                           lilty::ui::padding(lilty::ui::xy(5, 1)),
                                           lilty::ui::animateLayout()});
        box->setAlign(0.5f);
        if (i == 1)
            middle = box;
        lilty::ui::add(row, box);
    }

    // 换对齐方式，三个盒子会滑到新位置而不是瞬移。
    static const char *const kAligns[] = {"Start", "Center", "End", "Between"};
    Select *align = new Select("Main align", kAligns, 4, 0);
    align->onChange = [row](int, int to)
    {
        static const lilty::ui::MainAlign kTo[] = {lilty::ui::MainStart, lilty::ui::MainCenter,
                                                   lilty::ui::MainEnd, lilty::ui::MainSpaceBetween};
        row->setMainAlign(kTo[to]);
        return true;
    };

    // 让中间的盒子吃掉多余宽度，另外两个保持原尺寸。
    CheckBox *grow = new CheckBox("Grow box 2", false);
    grow->onCheck = [middle](bool, bool to)
    {
        middle->setGrow(to ? 1.0f : 0.0f);
        return true;
    };

    lilty::ui::Flex *col = new lilty::ui::Flex(lilty::ui::Vertical);
    col->setGap(2);
    lilty::ui::add(col, row);
    lilty::ui::add(col, align);
    lilty::ui::add(col, grow);

    return newPage("Flex", col);
}

// ------------------------------------------------------------- Long list

static Node *makeLongList()
{
    List *list = new List();
    char buf[24];
    for (int i = 1; i <= 40; i++)
    {
        snprintf(buf, sizeof buf, "Row %02d", i);
        ListItem *row = new ListItem(buf);
        row->onClick = []()
        {
            logAction("行");
            lilty::widget::toast("Tapped");
            return true;
        };
        lilty::ui::add(list, row);
    }
    return newPage("Long list", list);
}

// ---------------------------------------------------------------- Popups

// showDrawer 从指定的那条边滑进来一个三项菜单，选中哪项就 toast 哪项。
static void showDrawer(lilty::widget::Edge edge)
{
    Drawer *d = new Drawer(edge, "Menu");
    static const char *const kItems[] = {"Profile", "Settings", "Sign out"};
    for (int i = 0; i < 3; i++)
    {
        ListItem *row = new ListItem(kItems[i]);
        row->onClick = [d, i]()
        {
            d->close();
            lilty::widget::toast(kItems[i]);
            return true;
        };
        lilty::ui::add(d, row);
    }
    d->open();
}

static Node *makePopups()
{
    List *list = new List();

    struct EdgeEntry
    {
        const char *title;
        lilty::widget::Edge edge;
    };
    static const EdgeEntry kEdges[] = {{"Drawer from bottom", lilty::widget::Bottom},
                                       {"Drawer from top", lilty::widget::Top},
                                       {"Drawer from left", lilty::widget::Left},
                                       {"Drawer from right", lilty::widget::Right}};
    for (int i = 0; i < 4; i++)
    {
        ListItem *row = new ListItem(kEdges[i].title);
        row->onClick = [i]()
        {
            logAction(kEdges[i].title);
            showDrawer(kEdges[i].edge);
            return true;
        };
        lilty::ui::add(list, row);
    }

    ListItem *dlg = new ListItem("Dialog");
    dlg->onClick = []()
    {
        logAction("Dialog");
        showDialog();
        return true;
    };
    lilty::ui::add(list, dlg);

    ListItem *tst = new ListItem("Toast");
    tst->onClick = []()
    {
        logAction("Toast");
        lilty::widget::toast("Saved to slot 2");
        return true;
    };
    lilty::ui::add(list, tst);

    return newPage("Popups", list);
}

// ------------------------------------------------------------------ Home

struct Demo
{
    const char *title;
    Node *(*build)();
};

static Node *makeHome()
{
    static const Demo kDemos[] = {{"Buttons", makeButtons},
                                  {"Toggles", makeToggles},
                                  {"Select", makeSelects},
                                  {"Keypad", makeKeypad},
                                  {"Shelf", makeShelf},
                                  {"Flex", makeFlexDemo},
                                  {"Long list", makeLongList},
                                  {"Popups", makePopups}};

    List *list = new List();
    for (size_t i = 0; i < sizeof kDemos / sizeof kDemos[0]; i++)
    {
        const char *title = kDemos[i].title;
        Node *(*build)() = kDemos[i].build;
        ListItem *item = new ListItem(title);
        item->onClick = [title, build]()
        {
            logAction(title);
            lilty::ui::push(build());
            return true;
        };
        lilty::ui::add(list, item);
    }
    return newPage("lilty", list);
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

void loop() { lilty::loop(&onFrame); }
