> **SUPERSEDED 2026-07-18** — the rewrite shipped, but the remaining
> unchecked tasks were overtaken by later changes rather than done as
> planned here: the double-press nav and swipe gestures were replaced by
> the click-vs-combo button model (`badge-click-nav`), the three-app
> scope grew a fourth app (USB Drive), and the LittleFS badge loader was
> replaced by the FAT `badgeimg` partition (`badge-usb-drive`). Kept for
> reference only; do not implement. The live specs are the ones synced
> from those later changes.

## Why

The M5Stack C152 StopWatch is currently running a five-app cyberpunk-style
firmware (Stopwatch / Badge / Compass / Balance / Voice Toy). The user has
rejected that design and wants a clean rewrite around three tightly-scoped
apps (Stopwatch / Balance / Badge) with a flat, Apple-Watch-inspired UI.
The reasons for the rewrite are concrete and explicit:

- **VoiceToy** was removed because its I²S bring-up races the CO5300 OLED
  power-on sequence.
- **Compass** was removed because it is redundant with Balance and the
  BMI270 has no magnetometer to do real north-finding.
- The **cyberpunk neon palette** (full cyan, full magenta, amber-on-black)
  is being retired in favour of a flat dark theme with calm cyan and peach
  accents.
- The **horizontal 5-icon carousel** does not suit the round face and was
  replaced by three large round-cornered cards arranged in a circular
  layout with the focused card centred and the two neighbours as small
  side hints.

The rewrite keeps the same hardware target, the same Python badge uploader,
and the same USB-CDC serial upload protocol (`LIST` / `BIMG` / `ERASE`).

## What Changes

- Rewrite `stopwatch_multiverse.ino` to register exactly three apps
  (Stopwatch, Balance, Badge) in a fixed order. Drop `CompassApp` and any
  remaining `VoiceToy` references (already absent from the source).
- Replace `include/theme.h` and `src/theme.cpp` with a flat theme:
  `BG` / `SURFACE` / `SURFACE_2` / `TEXT_HI` / `TEXT_LO` / `ACCENT` /
  `ACCENT_2` / `DANGER` palette (all 16-bit RGB565 literals) and new
  primitives `drawCard`, `drawButtonHint`, `drawHint`. Drop
  `drawRingSelector`, `drawStatusBar`, `drawNeonButton`, `drawToast`,
  `drawNeonLabel`.
- Rewrite `src/shell.cpp` so the launcher shows three large round-cornered
  cards arranged in a circular layout (focused card centre, two side
  hints). Keep KEYA / KEYB cycle, tap-to-enter, double-press-pop-back.
  Wire swipe-left / swipe-right to cycle (in addition to KEYA / KEYB).
- Rewrite the `draw()` methods of Stopwatch, Balance, Badge per the
  ASCII layouts in `docs/REWRITE-HANDOFF.md` §8.
- Toolchain: use FQBN
  `m5stack:esp32:m5stack_stopwatch:…PSRAM=opi,PartitionScheme=app3M_fat9M_16MB…`
  with the `partitions.csv` (`app0 @ 0x10000`, `littlefs` ending at
  `0xFF0000`) shipped alongside the sketch.
- Mount LittleFS with `LittleFS.begin(true, "/littlefs", 10, "littlefs")`
  (partition label `littlefs`, not `spiffs`).
- Open `M5IOE1` on `&M5.In_I2C` (internal bus, pins 47 / 48). Already
  correct in `src/ioexpander.cpp` per the previous session.

## Capabilities

### New Capabilities

- `app-shell` — launcher, input handling, screen gestures, app lifecycle,
  back navigation. Three apps in a fixed order: Stopwatch, Balance, Badge.
- `flat-ui-theme` — flat dark theme: palette, typography, primitive
  widgets (`drawCard`, `drawButtonHint`, `drawHint`), animation helpers.
- `stopwatch-app` — millisecond-precision timing app (start / stop / lap /
  reset) and flat Stopwatch UI per the handoff layout.
- `balance-app` — tilt / level detection app with bubble indicator and
  haptic feedback per the handoff layout.
- `badge-app` — slideshow of user-uploaded circular images persisted in
  LittleFS, with auto-advance and manual next / prev per the handoff
  layout.
- `badge-image-uploader` — host Python web app for circular-cropping and
  uploading badge images over serial.

### Removed Capabilities

- `compass-app` — removed. Compass was redundant with Balance; the BMI270
  has no magnetometer for true north-finding.
- `voice-toy-app` — removed. VoiceToy's I²S bring-up races the OLED
  power-on sequence.
- `cyberpunk-ui-theme` — removed. Replaced by `flat-ui-theme`. The neon
  palette is being retired.

### Modified Capabilities

_None._ This is a clean rewrite of the firmware; the delta specs are
written fresh.

## Impact

- **New code**: `stopwatch_multiverse/` sketch tree (rewritten);
  `tools/badge_uploader/` Python web app; `scripts/` build / flash
  helpers.
- **Toolchain**: `arduino-cli` on `PATH`. ESP32 platform
  `espressif32 @ 6.12.0`. Board FQBN
  `m5stack:esp32:m5stack_stopwatch` with `PSRAM=opi` — without
  `PSRAM=opi` the `M5GFX` power-on block skips the CO5300 display and
  the panel stays black.
- **Libraries**: `M5Unified`, `M5GFX`, `M5IOE1` (no longer need
  `M5PM1`; M5Unified manages the battery PMU internally).
- **Hardware**: round 466×466 AMOLED (CO5300, QSPI) + CST820B touch,
  BMI270 IMU on internal I²C, M5IOE1 vibration motor on internal I²C.
- **Out of scope**: magnetometer-based north-finding (BMI270 has none);
  OTA updates; voice recording / playback; BLE / Wi-Fi provisioning;
  cross-platform mobile companion.
