## Context

The M5Stack StopWatch (C152) is an ESP32-S3R8 wearable with a 1.75" round
AMOLED (CO5300, 466×466, QSPI, touch via CST820B), two programmable
buttons (KEYA = G2 / KEYB = G1) plus a power button, a BMI270 6-axis
IMU on internal I²C, an M5IOE1 IO expander on the same internal bus
(running the vibration motor on PWM channel 9), and a 450 mAh battery
managed by M5PM1.

The user's previous five-app cyberpunk firmware was rejected for the
four reasons laid out in the proposal: VoiceToy I²S races the OLED,
Compass was redundant, the neon palette was off-brand, and the
horizontal carousel did not suit the round face. The new design is a
three-app flat-UI rewrite on the same hardware, with the same Python
badge uploader and the same USB-CDC serial upload protocol.

## Goals / Non-Goals

**Goals**

- A single Arduino sketch (`stopwatch_multiverse`) that boots into the
  three-card launcher in under 3 s.
- Three apps — Stopwatch, Balance, Badge — sharing one flat theme and
  one input model.
- A consistent input model: KEYA / KEYB cycle (launcher); tap enters
  the focused app; double-press of KEYA or KEYB pops back to the
  launcher; swipe-left / swipe-right also cycles in the launcher.
- The two physical buttons are the *only* input in the Stopwatch app
  — the UI must show their current action (`Start` / `Stop`,
  `Lap` / `Reset`) as a small bottom-of-screen hint.
- Repeatable `arduino-cli` build & upload flow that does not require
  the Arduino IDE.

**Non-Goals**

- Magnetometer-based north-finding (BMI270 has none). Compass is
  removed.
- Voice record / playback (VoiceToy is removed — I²S raced the OLED).
- BLE / Wi-Fi provisioning beyond the badge uploader's serial-USB
  bridge.
- OTA updates, deep-sleep optimisation, or production hardening for
  a wearable product.
- Cross-platform mobile companion. The badge uploader stops at a
  desktop browser.

## Decisions

### D1 — Arduino-CLI over PlatformIO / ESP-IDF

The user has Arduino-CLI on `PATH` and explicitly wants an
Arduino-based program. The official StopWatch PlatformIO config
maps cleanly to `arduino-cli compile --fqbn
m5stack:esp32:m5stack_stopwatch` with `PSRAM=opi` and
`PartitionScheme=app3M_fat9M_16MB`. ESP-IDF rejected — the user has
explicitly moved off the IDF demo path.

### D2 — Three apps only

VoiceToy (I²S race) and Compass (redundant with Balance, no
magnetometer) are dropped permanently. The remaining apps —
Stopwatch, Balance, Badge — cover the user's day-to-day needs without
the integration risk of ES8311 audio.

### D3 — Single-process cooperative app model

Each app is a `class App { virtual void onEnter(); virtual void
onTick(); virtual void onInput(...); virtual void onExit(); }`
registered in a static array. The launcher owns the loop and
dispatches ticks + input to the active app. Simplest correct model
for three tiny apps on a single core; avoids FreeRTOS task
synchronisation complexity.

### D4 — Input model: KEYA / KEYB + tap + swipe + double-press

- KEYA short press: previous app / decrement / left.
- KEYB short press: next app / increment / right.
- Touch tap (CST820B): confirm / enter.
- Touch swipe (left or right, ≥ 80 px / 200 ms): cycle apps.
- KEYA or KEYB double press (within 350 ms): pop to launcher.

The two physical buttons stay the *only* input inside the Stopwatch
app. KEYA = start / stop toggle. KEYB = lap when running, reset when
stopped. Inside the Badge app KEYA = previous, KEYB = next, tap =
pause / resume.

### D5 — Storage layout: LittleFS in a `app0`-at-0x10000 partition

Use `partitions.csv` (same layout as the IDE default
`app3M_fat9M_16MB` but with `littlefs` instead of FATFS) so the
bootloader finds `app0` at `0x10000`. A factory partition at `0x20000`
is a PlatformIO convention that does **not** apply on the Arduino
toolchain — without this fix the bootloader refuses to boot and the
panel stays black. Badge images are stored as 466×466 RGB565 raw
blobs (≈ 434 KB each); up to 4 fit in the ~10 MB partition.

### D6 — Flat theme as the single source of visual truth

`theme.h` defines a `theme` namespace with eight palette constants
(`BG`, `SURFACE`, `SURFACE_2`, `TEXT_HI`, `TEXT_LO`, `ACCENT`,
`ACCENT_2`, `DANGER`), a small caption font and a large mono-digit
font, and three primitive widgets (`drawCard`,
`drawButtonHint`, `drawHint`). Every app calls theme
primitives — no app owns its own fonts or colours. *All literals are
16-bit RGB565*; 24-bit hex literals would be silently truncated by
the compiler, which is the bug that killed the neon-palette launch.

### D7 — Badge image uploader: Python + Flask + Pillow + pyserial

Unchanged from the previous design. `tools/badge_uploader/server.py`
runs a local Flask server (default `127.0.0.1:5000`); the HTML page
renders a `<canvas>` masked to a 466×466 circle with drag-pan and
wheel-zoom; on "Send to watch" the server crops the image to the
circle, anti-aliases the edge, encodes to RGB565 little-endian, and
writes it to LittleFS via the firmware's serial protocol.

### D8 — Vibration motor stays on M5IOE1 PWM channel 9

The single shared `M5IOE1` instance in `src/ioexpander.cpp` is opened
on `&M5.In_I2C` (internal bus, pins 47 / 48). The Balance app flips
the duty between 0 and ~60 % at 2 Hz while the device is
`UNBALANCED`. The IO expander init fails silently if the chip is not
present; the Balance app tolerates a missing vibrator by leaving the
duty at 0.

### D9 — Build & flash as a single shell command

`scripts/flash.sh` runs `arduino-cli core install esp32:esp32@6.12.0`
(idempotent), `arduino-cli lib install M5Unified M5GFX M5IOE1`,
`arduino-cli compile --fqbn m5stack:esp32:m5stack_stopwatch:…
PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M…`,
`arduino-cli upload --port <auto-detected>` and optionally spawns
the badge uploader.

## Risks / Trade-offs

- **[Risk] VoiceToy removed without a replacement.**
  Mitigation: the user's stated workflow doesn't need voice
  notes on a watch; a future change can re-add a working VoiceToy.
- **[Risk] Bolt-on UI style drift when the user adds an app later.**
  Mitigation: enforce that every app `#include`s `theme.h` and uses
  only `theme::*` colours and primitives — no app defines its own
  colour values.
- **[Risk] Swipe gesture misfires on the round screen.**
  Mitigation: require ≥ 80 px of horizontal travel across ≥ 200 ms
  and reject a tap if the same press also crosses that threshold.
  Ignore touches within 8 px of the bezel.
- **[Risk] LittleFS wear from repeated badge uploads.**
  Mitigation: cap to 4 image slots, warn on overwrite, and rely on
  LittleFS's wear-leveling for the small-but-frequent log writes.
- **[Risk] The serial protocol race when the uploader and the user
  both replug.** Mitigation: every `BIMG` transfer starts with the
  `BIMG` magic + exact size; the firmware drops malformed frames;
  the uploader retries on `ERR`.
- **[Risk] Partition scheme off-by-one (factory @ 0x20000).**
  Mitigation: the bootloader-required `app0` partition is at
  `0x10000` in `partitions.csv`; documented in
  `docs/REWRITE-HANDOFF.md` §5b.

## Migration Plan

- New code lives at `stopwatch_multiverse/` next to the previous
  implementation; the previous implementation is overwritten in
  place.
- `scripts/flash.sh` is the single deploy command. The previous
  firmware is overwritten on the device — there is no rollback path
  inside this change; restoring the factory firmware is a separate
  concern (re-flash via M5Burner).
- Rollback strategy: re-flash the official M5Burner "出厂固件"
  referenced on the StopWatch docs page.

## Open Questions

- Should the Badge app auto-advance by default, or always require a tap
  to advance? (Default: auto-advance every 5 s, tap to pause / resume
  — keep the existing behaviour.)
- Should the launcher show the focused app's button-hint footer in the
  launcher (preview of what KEYA / KEYB do in that app), or only inside
  each app? (Default: footer only inside the app, launcher shows
  `KEYA prev   KEYB next`.)
- Should we add a fifth app later (e.g. a working VoiceToy, a
  Pomodoro timer, a heart-rate demo) once the I²S race is fixed?
