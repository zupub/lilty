# ESP32-S3 + SH1106 + rotary encoder

The reference port: an ESP32-S3-DevKitC-1 driving a 1.3" 128×64 SH1106 OLED over
I2C, with a rotary encoder (plus its push switch) and one extra BACK button.

This is the hardware the library was written on, so it doubles as the smoke test
for anything that touches layout, focus navigation or the layer stack.

## Wiring

All pins avoid USB (19/20), JTAG (39–42) and the PSRAM range (26–32).

| Signal | GPIO | Note |
| --- | --- | --- |
| OLED SDA | 8 | most modules carry their own 4.7k pull-ups |
| OLED SCL | 9 | |
| Encoder A | 14 | internal pull-up, other leg to GND |
| Encoder B | 15 | same |
| Encoder SW | 16 | the shaft push switch, to GND when pressed |
| BACK | 17 | separate button, other leg to GND |

Address defaults to `0x3C`. Some modules expose a jumper that moves it to
`0x3D`; the sketch probes both before giving up.

## Build and flash

```sh
pio run -d examples/esp32s3-sh1106-rotary
pio run -d examples/esp32s3-sh1106-rotary -t upload
pio device monitor -b 115200
```

The `platformio.ini` uses `lib_deps = symlink://../..` so it builds straight
against the library source in this repository.

## What you should see

Four rows on the home screen: **Controls**, **Long list**, **Dialog**, **Toast**.

- **Rotate** — move the focus ring. On the last row of a list it wraps.
- **Push the shaft** — activate. On the *Dialog* row it opens a modal with two
  side-by-side buttons; the encoder now steps between **OK** and **Cancel**
  horizontally, because the focus navigation translates Up/Down into Left/Right
  inside a horizontal container.
- **BACK** — close the dialog, or slide the current page back out. On the root
  page it just nudges.

Every activation also prints one line to the serial console, e.g. `[点击] Dialog OK`.
That is the fastest way to tell "the key never arrived" from "the callback ran
but the toast is not visible".

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| Screen stays black, serial prints `扫描 I2C 总线` and no address answers | SDA and SCL are swapped, or the module is not on 3V3. Check the scan output first: an empty scan is wiring, `0x3C` present but no pixels is the init sequence. |
| Nothing on screen, scan finds `0x3D` | The module's address jumper is in the other position. |
| Pixels appear but the image is shifted two columns | You are driving an SSD1306 with the SH1106 driver. SH1106 has 132 columns of RAM and the visible 128 start at column 2. |
| Rotation goes the wrong way | Call `nav.setReversed(true)`. |
| Encoder double-steps | The detent count is off; see `takeDetents()` in `src/input/rotary.cpp`. |
