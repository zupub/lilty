# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.1] - 2026-11-01

### Added

- `Scroll::scrollTo(x, y)` jumps straight to a scroll position, clamped to the
  content bounds. Navigation that does not go through the arrow keys needs it to
  snap back to the top when the focus returns to the first item: `reveal` only
  guarantees the focused node is visible, not the heading above it.

### Fixed

- `ListItem` was built on a vertical `Flex`, so every row with a trailing control
  stacked the title and the control on two lines and measured 22 px instead of
  11 px. `CheckBox`, `Radio` and `Select` now read on a single line, as intended,
  and every list fits roughly twice as many rows on screen.

### Changed

- `examples/esp32s3-sh1106-rotary` grew from four pages to eight, mirroring the
  `gallery` example of the original Go project.

## [0.1.0] - 2026-11-01

First public release. A C++ rewrite of the Go / TinyGo
[lilt](https://github.com/chhongzh/lilt) project by
[chhongzh](https://github.com/chhongzh) (`lilt-0.2.0`).

### Added

- `core/gfx` — 1-bit raster primitives, `Color` / `Rect` / `Canvas`, and the
  `Screen` interface a display backend implements.
- `core/anim` — damped spring integrator with target, velocity and rest
  threshold, plus the global `Value` scheduler.
- `core/font5x7` — 95 glyphs, 5×7 in a 6×8 cell, kept in flash.
- `core/ui` — `Node` tree, `Flex` / `Stack` / `Grid` layout, logical-to-physical
  scaling, the layer stack with full-page and popup transitions, d-pad focus
  navigation scored by direction and distance, marquee and scroll containers.
- `core/widget` — `Label`, `Button`, `CheckBox`, `Radio` / `RadioGroup`, `Select`,
  `List` / `ListItem`, `Scroll`, `Drawer`, `Dialog` and `toast()`.
- `display/sh1106_i2c` — SH1106 128×64 over I2C with address probing and a shadow
  buffer for dirty-page writes.
- `input/rotary` — quadrature decoder with detent accumulation, hold and repeat,
  driving `ui::emit`.
- `examples/esp32s3-sh1106-rotary` — the reference implementation, four demo pages.
- `examples/minimal` — the smallest runnable sketch, meant as a starting point
  for a new board.
- `test/test_core` — desktop unit tests running against an in-memory `Screen`,
  plus `pio test -e native` and a GitHub Actions workflow that builds both
  examples and runs the tests.

### Fixed

- Unqualified `apply(this, opts)` in the `Flex`, `Stack` and `Grid` constructors
  resolved to `std::apply` through ADL whenever the translation unit was built as
  C++17, silently dropping every option. Now qualified as `ui::apply`.
  (Arduino core builds with gnu++11, which has no `std::apply`, so the bug was
  invisible on the target.)
- Focus navigation stalled in horizontal containers because an encoder only
  produces `Up` / `Down`. They are now translated to `Left` / `Right` inside a
  horizontal parent, and the last focusable child of a container wraps to the
  first.
- A popup opened while the focus still sat on the page below never received it.
  The first turn of the encoder now moves the focus into the new layer.
- `toast()` kept a raw pointer to the last toast for de-duplication. Closing that
  layer frees the label, leaving the pointer dangling. It is now cleared from the
  label's destructor.

### Changed

- The library was renamed from `lilt` to `lilty`, and the namespace with it, to
  keep the C++ rewrite distinguishable from the original Go project.
- `src/` is split into `core/`, `display/` and `input/` so a port touches only the
  last two.
- `lilty::loop` no longer depends on `<Arduino.h>`; it uses `std::chrono` outside
  Arduino, which is what makes the desktop tests possible.

[Unreleased]: https://github.com/zupub/lilty/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/zupub/lilty/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/zupub/lilty/releases/tag/v0.1.0
