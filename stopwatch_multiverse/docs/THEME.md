# Cyberpunk UI Theme

`include/theme.h` is the single source of truth for colours, fonts and reusable widgets. Apps **must** include `theme.h` and **must not** define their own colours or load their own fonts.

## Palette

| Constant       | Value (RGB565) | Use                                     |
| -------------- | -------------- | --------------------------------------- |
| `CYAN_NEON`    | `0x00FFE5`     | Primary accent, default text            |
| `MAGENTA_NEON` | `0xFF2BD6`     | Secondary accent, non-focused outlines  |
| `AMBER_WARN`   | `0xFFB347`     | Warnings, recording indicator           |
| `INK_BG`       | `0x05060A`     | Background — apps call `fillScreen(INK_BG)` at the start of every frame |

Apps pick one of these for their launcher icon. No new colour values are allowed.

## Fonts

Two registered fonts are available:

| Use        | Glyph size |
| ---------- | ---------- |
| Small label | 7 × 12     |
| Large display | 16 × 24   |

Apps select the appropriate size via `M5.Lcd.setTextSize(...)` (1 = small, 2 = large). No app loads its own font.

## Layout

| Constant             | Value | Meaning                              |
| -------------------- | ----- | ------------------------------------ |
| `SCREEN_W` / `SCREEN_H` | 466 | AMOLED round display dimensions     |
| `STATUS_BAR_H`       | 28    | Reserved top band — apps draw below  |
| `BEZEL_DEADZONE_PX`  | 8     | Touches within this radius are dropped |

## Animation

* `TARGET_FPS` — 30 frames per second.
* `frameReady()` — returns `true` once per `FRAME_BUDGET_MS` (33 ms). Call at the top of `onTick()` to throttle drawing.
* `perfMode` — boolean flag (default `true`). When `false`, apps should skip neon glow and particle effects to save SPI bandwidth.

## Widget primitives

All widgets draw to `theme::display()` (which returns `M5.Lcd`).

### `drawRingSelector(cx, cy, r, progress)`

Renders the focus ring used in the launcher carousel, the compass dial and the stopwatch sweep.

* `cx`, `cy`, `r` — centre and radius in pixels.
* `progress` — 0.0 to 1.0; the inner filled arc spans that fraction of the circle, starting at 12 o'clock and going clockwise.

### `drawStatusBar(batteryPct, appName)`

Reserves the top 28-pixel band. `batteryPct` is 0–100; under 20 % the readout switches to `AMBER_WARN`. Apps must call this on every frame they want a status bar (typically every tick).

### `drawNeonButton(x, y, w, h, label, pressed)`

Pill-shaped button with a glow halo. `pressed` switches the inner fill to the accent colour and the label to the background — handy for toggle / press-and-hold affordances.

### `drawToast(text)`

Centre-justified, dim-background toast drawn near the bottom of the screen. Safe to call every frame; the toast persists for one frame. Pair with `delay(...)` in your app if you want it to linger.

### `drawNeonLabel(cx, cy, text, color)`

Tiny centred label using the small font and an explicit palette colour. Useful for "GYRO HEADING" or unit readouts.

## Adding a new app

1. Subclass `App` and implement `onEnter`, `onTick`, `onInput`, `onExit`.
2. Provide `name()` and `iconColor()` (must be one of the four palette values).
3. Register your app in the static array in `stopwatch_multiverse.ino` in the position you want in the carousel.
4. Use only the four palette colours. Use only the two registered fonts. Use the widget primitives — don't draw raw pixels where a primitive exists.
5. Respect the FPS cap (`theme::frameReady()`).
6. Stay non-blocking in `onTick()` — yield with `delay(0)` or do per-frame chunked work.