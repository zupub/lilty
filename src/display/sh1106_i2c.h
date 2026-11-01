// sh1106_i2c: 1.3 吋 128x64 SH1106 OLED 的 I2C 驱动。
//
// SH1106 不是 SSD1306：它内部有 132 列显存，可见的 128 列从第 2 列开始，
// 所以每页写数据前要先把列地址加 2。初始化里还必须打开片内 DC-DC。
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "core/gfx.h"

namespace lilty
{
namespace display
{

class Sh1106I2C : public gfx::Screen
{
  public:
    explicit Sh1106I2C(uint8_t addr = 0x3C, TwoWire *wire = &Wire);

    // begin 初始化总线与面板。sda / scl 传 -1 用板子的默认引脚。返回 false
    // 表示地址上没有应答。
    bool begin(int sda = -1, int scl = -1, uint32_t hz = 400000);
    // setAddress 换 I2C 地址，要在 begin 之前调用。模块背面把 0x78 的跳线
    // 焊到 0x7A 就是 0x3D。
    void setAddress(uint8_t a)
    {
        addr_ = a;
    }
    // setFlipped 把画面旋转 180 度。只改软件映射，不用重发初始化序列。
    void setFlipped(bool flip)
    {
        flipped_ = flip;
    }
    void setContrast(uint8_t v);
    // setInverted 让面板整体反色，和光标的局部反色无关。
    void setInverted(bool on);

    int16_t width() const override
    {
        return 128;
    }
    int16_t height() const override
    {
        return 64;
    }
    void setPixel(int16_t x, int16_t y, const gfx::Color &c) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, const gfx::Color &c) override;
    bool canInvert() const override
    {
        return true;
    }
    void invertRect(int16_t x, int16_t y, int16_t w, int16_t h) override;
    void flush() override;

    // ready 报告面板是否还能通信。
    bool ready() const
    {
        return ready_;
    }

  private:
    void cmd(uint8_t c);
    void cmds(const uint8_t *list, uint8_t count);
    void pushPage(uint8_t page);

    TwoWire *wire_;
    uint8_t addr_;
    // buf_ 是整屏显存，一页 8 行，每字节的 bit0 是最上面那行。
    uint8_t buf_[8 * 128];
    // shadow_ 是上一次推给面板的内容。flush 逐页比较，只推变化了的页，
    // 于是每帧全屏重画也不会把 8 页全推一遍。
    uint8_t shadow_[8 * 128];
    bool flipped_ = false;
    bool ready_ = false;
};

} // namespace display
} // namespace lilty
