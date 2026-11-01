// 这个后端只在 Arduino 下存在。native 构建时整个文件退化成空的，于是同一个
// src/display 目录里可以再放一个 PC 用的后端（见 gfx_sdl.cpp）。
#ifdef ARDUINO

#include "display/sh1106_i2c.h"

#include <string.h>

namespace lilty
{
namespace display
{

// kColOffset 是 SH1106 显存第 2 列对应可见区第 0 列。
static constexpr uint8_t kColOffset = 2;
// kChunk 是每次 I2C 传输带多少个显示字节。加上控制字节后仍在 Arduino
// Wire 的默认 128 字节缓冲之内。
static constexpr int kChunk = 64;

// kInit 是初始化命令序列。0xAD 0x8B 打开片内 DC-DC，这一步 SSD1306 的
// 序列里没有，缺了屏就不亮。
static const uint8_t kInit[] = {
    0xAE,       // 关显示
    0xD5, 0x80, // 时钟分频与振荡频率
    0xA8, 0x3F, // 复用率 64
    0xD3, 0x00, // 显示偏移 0
    0x40,       // 起始行 0
    0xAD, 0x8B, // 打开片内 DC-DC
    0x32,       // 泵电压 8.0V
    0xA1,       // 段重映射，左右翻
    0xC8,       // COM 反向扫描，上下翻
    0xDA, 0x12, // COM 引脚配置
    0x81, 0xCF, // 对比度
    0xD9, 0xF1, // 预充电周期
    0xDB, 0x40, // VCOMH 电压
    0xA4,       // 按显存显示
    0xA6,       // 正常反色
    0xAF,       // 开显示
};

Sh1106I2C::Sh1106I2C(uint8_t addr, TwoWire *wire) : wire_(wire), addr_(addr)
{
    memset(buf_, 0, sizeof buf_);
    memset(shadow_, 0xFF, sizeof shadow_);
}

void Sh1106I2C::cmd(uint8_t c)
{
    cmds(&c, 1);
}

void Sh1106I2C::cmds(const uint8_t *list, uint8_t count)
{
    if (!ready_)
        return;
    wire_->beginTransmission(addr_);
    wire_->write(uint8_t(0x00)); // 控制字节：后面全是指令
    for (uint8_t i = 0; i < count; i++)
        wire_->write(list[i]);
    if (wire_->endTransmission() != 0)
        ready_ = false;
}

bool Sh1106I2C::begin(int sda, int scl, uint32_t hz)
{
    if (sda >= 0 && scl >= 0)
    {
        wire_->begin(sda, scl, hz);
    }
    else
    {
        wire_->begin();
        wire_->setClock(hz);
    }
#if defined(ESP32)
    // 一页 128 字节加上控制字节会超过默认缓冲，直接要一块更大的。
    wire_->setBufferSize(kChunk + 8);
#endif
    // 先探一下地址上有没有应答，省得后面每条命令超时。
    wire_->beginTransmission(addr_);
    if (wire_->endTransmission() != 0)
    {
        ready_ = false;
        return false;
    }
    ready_ = true;
    cmds(kInit, uint8_t(sizeof kInit));
    memset(buf_, 0, sizeof buf_);
    // 和显存相反，于是第一次 flush 会把 8 页都推一遍，上电后的随机内容
    // 被清掉。
    memset(shadow_, 0xFF, sizeof shadow_);
    flush();
    return ready_;
}

void Sh1106I2C::setContrast(uint8_t v)
{
    const uint8_t seq[2] = {0x81, v};
    cmds(seq, 2);
}

void Sh1106I2C::setInverted(bool on)
{
    cmd(on ? 0xA7 : 0xA6);
}

void Sh1106I2C::setPixel(int16_t x, int16_t y, const gfx::Color &c)
{
    int16_t px = flipped_ ? int16_t(127 - x) : x;
    int16_t py = flipped_ ? int16_t(63 - y) : y;
    if (px < 0 || py < 0 || px >= 128 || py >= 64)
        return;
    uint16_t i = uint16_t(uint16_t(py >> 3) * 128u + uint16_t(px));
    uint8_t bit = uint8_t(1u << (py & 7));
    if (gfx::isLit(c))
        buf_[i] |= bit;
    else
        buf_[i] &= uint8_t(~bit);
}

void Sh1106I2C::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, const gfx::Color &c)
{
    if (w <= 0 || h <= 0)
        return;
    int16_t x0 = x, y0 = y;
    int16_t x1 = int16_t(x + w), y1 = int16_t(y + h);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > 128)
        x1 = 128;
    if (y1 > 64)
        y1 = 64;
    if (x1 <= x0 || y1 <= y0)
        return;
    bool lit = gfx::isLit(c);
    uint8_t litMask = lit ? 0xFF : 0x00;
    for (int16_t py = y0; py < y1; py++)
    {
        int16_t row = flipped_ ? int16_t(63 - py) : py;
        uint16_t base = uint16_t(uint16_t(row >> 3) * 128u);
        uint8_t bit = uint8_t(1u << (row & 7));
        uint8_t *line = buf_ + base;
        if (!flipped_)
        {
            for (int16_t px = x0; px < x1; px++)
            {
                if (litMask)
                    line[px] |= bit;
                else
                    line[px] &= uint8_t(~bit);
            }
        }
        else
        {
            for (int16_t px = x0; px < x1; px++)
            {
                int16_t col = int16_t(127 - px);
                if (litMask)
                    line[col] |= bit;
                else
                    line[col] &= uint8_t(~bit);
            }
        }
    }
}

void Sh1106I2C::invertRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (w <= 0 || h <= 0)
        return;
    int16_t x0 = x, y0 = y;
    int16_t x1 = int16_t(x + w), y1 = int16_t(y + h);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > 128)
        x1 = 128;
    if (y1 > 64)
        y1 = 64;
    if (x1 <= x0 || y1 <= y0)
        return;
    for (int16_t py = y0; py < y1; py++)
    {
        int16_t row = flipped_ ? int16_t(63 - py) : py;
        uint16_t base = uint16_t(uint16_t(row >> 3) * 128u);
        uint8_t bit = uint8_t(1u << (row & 7));
        uint8_t *line = buf_ + base;
        for (int16_t px = x0; px < x1; px++)
        {
            int16_t col = flipped_ ? int16_t(127 - px) : px;
            line[col] ^= bit;
        }
    }
}

// pushPage 把一页 128 列推给面板。SH1106 没有水平寻址模式，只能一页一页
// 地设好列地址再连续写。
void Sh1106I2C::pushPage(uint8_t page)
{
    wire_->beginTransmission(addr_);
    wire_->write(uint8_t(0x00));
    wire_->write(uint8_t(0xB0 | page));
    wire_->write(uint8_t(kColOffset & 0x0F));
    wire_->write(uint8_t(0x10 | (kColOffset >> 4)));
    if (wire_->endTransmission() != 0)
    {
        ready_ = false;
        return;
    }
    const uint8_t *src = buf_ + uint16_t(page) * 128u;
    int done = 0;
    while (done < 128)
    {
        int chunk = 128 - done;
        if (chunk > kChunk)
            chunk = kChunk;
        wire_->beginTransmission(addr_);
        wire_->write(uint8_t(0x40)); // 控制字节：后面全是显示数据
        for (int i = 0; i < chunk; i++)
            wire_->write(src[done + i]);
        if (wire_->endTransmission() != 0)
        {
            ready_ = false;
            return;
        }
        done += chunk;
    }
}

void Sh1106I2C::flush()
{
    if (!ready_)
        return;
    for (uint8_t page = 0; page < 8; page++)
    {
        uint16_t base = uint16_t(page) * 128u;
        const uint8_t *cur = buf_ + base;
        uint8_t *old = shadow_ + base;
        if (memcmp(cur, old, 128) == 0)
            continue;
        pushPage(page);
        if (!ready_)
            return;
        memcpy(old, cur, 128);
    }
}

} // namespace display
} // namespace lilty

#endif // ARDUINO
