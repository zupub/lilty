# lilty

<div align="center">

![PlatformIO](https://img.shields.io/badge/PlatformIO-espressif32-blue)
![Arduino](https://img.shields.io/badge/framework-Arduino-00979D)
![MCU](https://img.shields.io/badge/MCU-ESP32--S3%20N16R8-red)
![Display](https://img.shields.io/badge/display-SH1106%20128x64-lightgrey)

![C++](https://img.shields.io/badge/C%2B%2B-11-00599C)
![native tests](https://img.shields.io/badge/native%20tests-12%20passing-brightgreen)
![License](https://img.shields.io/badge/License-MIT-green)

</div>

<p align="center">
  <strong><a href="README.zh-CN.md">中文</a> | English</strong>
</p>

A small GUI framework for 128×64 monochrome OLEDs. It is a C++ port of
**[lilt](https://github.com/chhongzh/lilt)** — the Go / TinyGo project by
[chhongzh](https://github.com/chhongzh), version `lilt-0.2.0`.

`lilty` only means "the C++ version of lilt": the structure, the feel and the API
all follow the original. Go interfaces became a `Node` base class, channels became
synchronous callbacks, and the garbage collector became explicit `delete`. The
goals are unchanged: **built for a d-pad and a knob**, **every animation is
spring-driven**, and **pages and popups share one layer stack**.

- Logical coordinates are 128×64 and scale to the panel by `min(w/128, h/64) × zoom`
- A press only gives feedback, the release fires (`KeyEvent::fires()`)
- One global cursor: it stretches toward the target, shrinks when it lags, and is
  held back by a rubber band at the edges
- Focus navigation is scored like Android's FocusFinder (`13·major² + minor²`), so
  it does not depend on tree order
- An encoder only produces "one notch up / one notch down"; inside a horizontal
  container those are translated to left / right automatically
- No RTTI, no exceptions, no `std::vector` / `std::string`; nodes are `new`ed and
  `delete`d explicitly

## Layout

Three layers. `core` never touches hardware; `display` and `input` are the
per-board backends you write once per device.

```
lilty/
├── library.json  library.properties      PlatformIO / Arduino IDE metadata
├── platformio.ini                        only for `pio test`, see Tests
├── README.md  README.zh-CN.md  CHANGELOG.md  LICENSE
├── src/
│   ├── lilty.h  lilty.cpp       the begin() / loop() glue (still hardware-free)
│   ├── core/                    no hardware: swap one Screen and it runs on a PC
│   │   ├── gfx.h  gfx.cpp       Color / Rect / Screen / Canvas
│   │   ├── font5x7.cpp          5×7 ASCII glyphs (lives in flash, not in RAM)
│   │   ├── anim.h  anim.cpp     Spring / Value / global scheduler
│   │   ├── ui.h                 Node / Flex / Stack / Grid / options / layers / input
│   │   ├── ui_core.cpp          Node, layout, Flex / Stack / Grid, options
│   │   ├── ui_runtime.cpp       layer stack, focus navigation, cursor, frame()
│   │   └── widget.h  widget.cpp Label / List / Button / CheckBox / Radio /
│   │                            Select / Drawer / Dialog / Toast
│   ├── display/                 display backend: implement gfx::Screen
│   │   └── sh1106_i2c.h  .cpp   SH1106 128×64 over I2C (Arduino)
│   └── input/                   input backend: push Keys into lilty::ui::emit
│       └── rotary.h  .cpp       rotary encoder plus two buttons (Arduino)
├── examples/                    each subfolder is a standalone PlatformIO project
│   ├── esp32s3-sh1106-rotary/   the reference port, four demo pages
│   └── minimal/                 one screen, one list; start a new port from here
├── test/test_core/              desktop unit tests with an in-memory Screen
└── .github/workflows/build.yml  builds both examples and runs the tests
```

There is exactly one platform-dependent line in `lilty.cpp`: reading a
microsecond timestamp. Arduino uses `micros()`, everything else uses
`std::chrono::steady_clock`. That is why even the frame loop runs on a PC, and why
`core/` is genuinely portable.

Two boundaries, and porting means filling in one file on either side:

| Layer | Depends on | What you do for new hardware |
| --- | --- | --- |
| `core/` | the C++ standard library, `<stdint.h>` | nothing |
| `display/` | `core/gfx.h` + your bus library | write one `Screen` subclass |
| `input/` | `core/ui.h` + your GPIO library | write one `poll(dt)` that pushes keys into `emit()` |

## Install

**PlatformIO** — once it is in the registry:

```ini
lib_deps = lilty
```

While working on the library itself, point at the source so edits need no
reinstall:

```ini
lib_deps = symlink://path/to/lilty
```

If you would rather not use `lib_deps`, add `-I path/to/lilty/src` to
`build_flags` and let the compiler see the sources directly.

**Arduino IDE** — download the repository as a zip and use
`Sketch > Include Library > Add .ZIP Library`, then `#include <lilty.h>`.
`library.properties` is filled in, so the library manager recognises it.

## Porting to another piece of hardware

### Display backend

Implement the five pure virtuals of `lilty::gfx::Screen`:

```cpp
namespace lilty {
namespace display {

class Ssd1306I2C : public gfx::Screen {
 public:
  bool begin(int sda = -1, int scl = -1);
  int16_t width() const override { return 128; }
  int16_t height() const override { return 64; }
  void setPixel(int16_t x, int16_t y, const gfx::Color& c) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, const gfx::Color& c) override;
  void flush() override;               // push the frame to the panel
  bool canInvert() const override { return true; }
  void invertRect(int16_t x, int16_t y, int16_t w, int16_t h) override;
};
```

The rules, all of which the SH1106 driver follows:

- **Out-of-range coordinates must be ignored** — no asserts, no wrapping. `Canvas`
  only clips when it has to; the driver is the last gate.
  `test_out_of_bounds_pixel_is_ignored` exists to keep it that way.
- On a 1-bit panel, use `gfx::isLit(c)` to decide whether a pixel is on. Do not
  compare `r`, `g` and `b` against a threshold yourself.
- When `canInvert()` returns `true` the cursor inverts its rect with `invertRect`;
  when it returns `false` (the default) the framework outlines it with
  `theme.accent` instead.
- `flush()` is called every frame. A 1 KB `memcmp` per page is cheap, so track
  dirty pages yourself (see `shadow_` in `sh1106_i2c.cpp`), otherwise you push
  1024 bytes every frame.
- Absorb any panel-RAM-to-visible-area offset inside the driver. The SH1106 has
  132 columns of RAM and the visible 128 start at column 2, so it writes
  `col + 2`; an SSD1306 (128 aligned columns) needs nothing.
- Do not forget the charge pump / DC-DC lines in the init sequence. The symptom of
  forgetting them is "the bus works but the screen stays black".

Then it is one line: `lilty::begin(&oled, page());`

### Input backend

There is no interface class. The contract is "push keys into `ui::emit` once per
frame":

```cpp
namespace lilty {
namespace input {

class Buttons {
 public:
  void begin(int up, int down, int ok, int back);
  void poll(float dt) {
    poller_.poll(ui::Key::Up, digitalRead(up) == LOW, dt);
    poller_.poll(ui::Key::Down, digitalRead(down) == LOW, dt);
    poller_.poll(ui::Key::OK, digitalRead(ok) == LOW, dt);
    poller_.poll(ui::Key::Back, digitalRead(back) == LOW, dt);
  }
 private:
  ui::KeyPoller poller_;
};

}  // namespace input
}  // namespace lilty
```

The rules:

- **A discrete "step"** (one encoder detent, a joystick flick, a swipe) must emit
  **one `Press` and one `Release`**. The whole framework has exactly one trigger
  semantic — fire on release — and a detent has no release of its own, so
  synthesise the pair. Spinning continuously just means more pairs.
- Keys with press-and-hold semantics (a power key, say) belong in `ui::KeyPoller`,
  which debounces and accelerates repeats (`repeatDelay` / `repeatInterval` /
  `accel`).
- Only six keys exist: `Up` / `Down` / `Left` / `Right` / `OK` / `Back`. Left and
  right are optional — `core` translates encoder steps into left / right inside a
  horizontal container.
- You never handle "go back" yourself. An unclaimed `Back` closes the top layer.

Then hook it up in `loop()`:

```cpp
void loop() { lilty::loop([](float dt) { nav.poll(dt); }); }
```

### Backend file naming

A device can have several backends. Name them `<controller>_<bus>` for displays and
after the input form for inputs, so the wiring is obvious from the filename:

```
src/display/                       src/input/
├── sh1106_i2c.h  .cpp   (here)     ├── rotary.h  .cpp      (here)
├── ssd1306_i2c.h  .cpp             ├── buttons.h  .cpp     discrete GPIO keys
├── ssd1306_spi.h  .cpp             ├── matrix.h   .cpp     key matrix
├── st7565_spi.h   .cpp             ├── joystick.h .cpp     ADC joystick
├── ili9341_spi.h  .cpp  (colour)   └── touch.h    .cpp     CST816 / FT6236
└── gfx_sdl.h      .cpp             (desktop debugging)
```

Class names follow the file in UpperCamelCase (`Sh1106I2C`, `Ssd1306Spi`, `Rotary`)
and always live in `lilty::display` / `lilty::input`. `core/` only knows the
abstractions declared in `gfx.h` and `ui.h`; it never includes anything from
`display/` or `input/`.

## Examples

`examples/esp32s3-sh1106-rotary` is the reference port — the library was written
on this exact hardware:

```sh
pio run -d examples/esp32s3-sh1106-rotary           # build
pio run -d examples/esp32s3-sh1106-rotary -t upload
pio device monitor -b 115200
```

Wiring on an `ESP32-S3-DevKitC-1` (every pin avoids USB 19/20, JTAG 39–42 and the
PSRAM range 26–32):

| Signal | GPIO | Note |
| --- | --- | --- |
| OLED SDA | 8 | most modules carry their own 4.7k pull-ups |
| OLED SCL | 9 | |
| Encoder A | 14 | internal pull-up, other leg to GND |
| Encoder B | 15 | same |
| Encoder SW | 16 | the shaft push switch, to GND when pressed |
| BACK | 17 | separate button, other leg to GND |

The address defaults to `0x3C`; a jumper on the back of the module moves it to
`0x3D`. If nothing answers, the sketch prints the bus scan — an empty scan is what
a swapped SDA/SCL looks like.

`examples/minimal` is the smallest runnable sketch: one screen, one list. Get that
one blinking first when you port to a new board.

Both `platformio.ini` files use `lib_deps = symlink://../..` to point back at this
repository, so `pio run -d examples/...` picks up library edits without publishing
anything.

## Minimal usage

```cpp
#include "lilty.h"
#include "display/sh1106_i2c.h"
#include "input/rotary.h"

static lilty::display::Sh1106I2C oled(0x3C);
static lilty::input::Rotary nav;

void setup() {
  oled.begin(8, 9);           // SDA, SCL
  nav.begin(14, 15, 16, 17);  // A, B, shaft switch, BACK
  lilty::setFps(60);
  lilty::begin(&oled, page());
}

void loop() { lilty::loop([](float dt) { nav.poll(dt); }); }
```

The hook passed to `lilty::loop` runs before `ui::frame`, so input sampled this
frame takes effect in the same frame.

## Tests

`core/` never touches hardware, so the whole UI pipeline runs on a PC:

```sh
pio test -e native
```

`test/test_core/test_main.cpp` ships a `MockScreen` that writes into a 1 KB bitmap
and implements nothing but the `gfx::Screen` virtuals. The cases cover the parts
that regress easily: rect clipping, the 1-bit threshold, Flex stacking and `grow`,
focus moving down a column, **cross-axis movement and wrap-around inside a
horizontal row**, focus landing on the first dialog button, focus restoration
across `push`/`pop`, actually painting pixels, and out-of-range pixels being
dropped.

## Building a UI

Nodes are always `new`ed; once attached to a parent they are `delete`d recursively
by the parent's destructor.

```cpp
auto* page = new lilty::ui::Flex(lilty::ui::Vertical, {
  lilty::ui::padding(lilty::ui::all(2)),
  lilty::ui::gap(2),
  lilty::ui::background(lilty::ui::theme.background),
});

auto* head = new lilty::widget::Label("Settings", {lilty::ui::fillWidth()});
head->setColor(lilty::ui::theme.accent);
lilty::ui::add(page, head);

auto* body = new lilty::ui::List();
body->setGrow(1);                    // take the remaining height
lilty::ui::add(page, body);
```

`ui::add(parent, child)` is `ui::attach(ui::contentOf(parent), child)`. Both
participate in layout; the only difference is which node the child hangs off —
`contentOf` returns the inner `content_` for `Scroll` / `List` and the node itself
for everything else. So use `add` by default, and `attach` only when you want to
skip the scrolling container and hang something off its shell (a pull-to-refresh
bar, a scrollbar — anything that should not move with the content).

Common options: `width` / `height` / `fill` / `fillWidth` / `fillHeight` / `grow` /
`padding` / `margin` / `gap` / `main` / `cross` / `focusable` / `hidden` / `clip` /
`background` / `border` / `radius` / `animateLayout` / `children` / `columns` (Grid).

Sizes are expressed with `Size`: `px` is a fixed number of logical pixels, `rel` is
a fraction of the parent's content box, `autoSize` measures the content (default).

```cpp
lilty::ui::width(lilty::ui::px(40));
lilty::ui::width(lilty::ui::rel(0.5f));      // half the parent's content width
lilty::ui::width(lilty::ui::autoSize());
```

## Navigation and pages

```cpp
lilty::ui::push(newPage);                       // slide in from the right
lilty::ui::pop();                               // slide back (BACK is wired up)
lilty::ui::open(node, lilty::ui::PopupDialog);  // popup / drawer (see the Popup enum)
lilty::ui::close(node);
lilty::ui::setFocus(row);                       // focus something explicitly
```

After `push` the focus lands on the first focusable node of the new page, and
`pop` returns it to the row you came from.

Focus moves on release, or on every repeat while a key is held. The key first
searches in its literal direction (FocusFinder scoring); if that finds nothing, it
is translated once according to the container's axis — up/down become left/right
inside a horizontal container, and the reverse in a vertical one. So on a vertical
list the knob is up/down, and on a side-by-side OK/Cancel it becomes left/right.
At the end of a container it wraps around to the other end.

## Components

| Component | Key API |
| --- | --- |
| `Label` | `setText` / `setFont` / `setColor` / `setAlign(0..1)` / `setMarquee` |
| `List` | `add(list, item)`; a `Scroll` that only scrolls vertically |
| `ListItem` | `title()` / `setTrailing(node)` / `activate` / `onClick` |
| `Button` | `onClick` (return `false` to fail and shake) / `setHold(seconds)` |
| `CheckBox` | `onCheck(from, to)`; return `false` to roll back |
| `Radio` + `RadioGroup` | `RadioGroup::onCheck` / `picked` |
| `Select` | opens a drawer of radio rows, `onChange(from, to)` |
| `Drawer` | `Drawer(Edge, title)`, `open()` / `close()` |
| `Dialog` | `Dialog(title, message, {ok, cancel})`, `open()` |
| `toast(text)` | slides up from the bottom, closes itself after two seconds |

Every callback is synchronous: `ui::emit` → the widget handles it → your `onClick`
runs → only then does the frame continue. So calling `push` / `open` / `delete`
inside a handler is fine, but **do not block** there.

## Writing your own widget

Derive from `ui::Node` and override what you need:

```cpp
Vec2 measure(Vec2 max) override;          // report your natural size
void layoutContent() override;            // lay out children yourself (with isLayouter)
void draw(gfx::Canvas& c) override;       // paint (coordinates are already pixels)
void drawOver(gfx::Canvas& c) override;   // paint after the cursor (badges, scrollbars)
bool handleKey(KeyEvent& e) override;     // consume a key, return true
bool isLeaf() const override { return true; }  // no children allowed
Rect cursorShape(Rect r) override;        // customise the focus ring
```

Call `ui::invalidate()` to request a repaint and `ui::relayout()` to request a new
pass.

## Differences from the Go version

| Go / TinyGo (lilt) | lilty | Why |
| --- | --- | --- |
| interfaces + type assertions | one `Node` base class + virtuals | the ESP32 Arduino core builds with `-fno-rtti` |
| `[]Node` slices | intrusive sibling list | no allocation, O(1) append, no shifting on delete |
| `Option func(Node)` | `std::function<void(Node*)>` | same call syntax |
| generic `OnChange[T]` | one `std::function<bool(T,T)>` per type | no template bloat, no overload ambiguity |
| goroutines + channels | single-threaded synchronous callbacks | runs without an RTOS task, saves stack |
| garbage collector | recursive `delete` from the parent | explicit ownership, predictable timing |
| `math.Exp` | `expNeg`, a Padé approximation | the S3 FPU is single precision; float64 is slow |
| tinyfont atlas | hand-drawn 5×7 cells | 111 lines of data, stays in flash |
| dirty-page `pageMask` | 1 KB shadow buffer, per-page `memcmp` | with full repaints every frame the mask is always dirty |
| touch / pointer events | none | the target hardware has a knob and two keys |

Also dropped:

- **The three loading dots.** Handlers run synchronously, so `loading()` is always
  false. `kLoadingDelay` is kept only so a desktop async backend can reuse the same
  widgets.
- **Multi-touch and gestures.** The whole input surface is six `Key`s.
- **The misc package.** Colour dithering and friends were not ported; on a 1-bit
  panel `isLit()` plus a threshold is enough.

One thing that had to be fixed rather than ported: `apply(this, opts)` must be
written as `ui::apply(this, opts)`. Unqualified, ADL finds `std::apply` under
C++17 (the associated namespace of `initializer_list` is `std`), and because
`std::apply` takes forwarding references it is the better match — every option
would be silently dropped. The desktop tests are what exposed this.

## Runtime knobs

```cpp
lilty::ui::setZoom(0.8f);      // call before lilty::begin; scale = min(w/128, h/64) * zoom
lilty::ui::scale();            // current physical pixels per logical pixel
lilty::setFps(30);             // save power; 0 = unlimited
lilty::anim::timeScale = .5f;  // global animation speed
lilty::ui::theme               // colours, fonts, radii, spacing — see Theme in ui.h
```

`ui::cursorLead / cursorTrail / cursorRelease / cursorLeap / cursorLean` tune the
cursor feel. They are `anim::Spring{response, damping, ease}`, the same shape as
SwiftUI's `animation(.spring())`: a larger `response` is slower, a smaller
`damping` is bouncier.

## Footprint

ESP32-S3, Arduino 2.0.14, PlatformIO release build, including all four demo pages:

```
RAM:   7.1% (used 23320 bytes from 327680 bytes)
Flash: 9.6% (used 319241 bytes from 3342336 bytes)
```

Of that, the framework itself keeps a 1 KB framebuffer (128×64 / 8) plus a 1 KB
shadow buffer. The rest scales with the number of nodes. Glyph data lives in flash
and costs no RAM.

## Acknowledgements

Thanks to [chhongzh](https://github.com/chhongzh) for
**[lilt](https://github.com/chhongzh/lilt)**, the Go / TinyGo original this port
is built on.

The architecture this library reproduces — the `Node` tree, the layer stack shared
by pages and popups, the FocusFinder-style scoring, the spring-driven feel, and the
shape of the API — is their design. What lives here is a translation into C++ plus
the adjustments listed under *Differences from the Go version*. If your target is
Go or TinyGo, use the original.

## License

MIT, see [LICENSE](LICENSE). The behaviour and API design belong to the author of
the original lilt project; this repository is only its C++ port.
