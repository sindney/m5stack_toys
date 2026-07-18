# StopWatch Multiverse — UI Rewrite Handoff

> **Historical document (2026-07-17).** The firmware has since moved on:
> LittleFS is replaced by a raw FAT `badgeimg` partition exposed over
> USB mass storage (see `README.md` and
> `openspec/changes/badge-usb-drive/`), the serial upload protocol in
> §9 is deleted, and the FQBN uses `USBMode=default` (TinyUSB). Keep
> this for the UI/layout rationale; don't follow its storage or
> transport instructions.

This document is the complete starting point for the UI rewrite work
the user has commissioned. **Read this first** before touching any code.

## 1. Hardware

- **Board**: M5Stack C152 StopWatch
- **Display**: 466 × 466 **round** AMOLED (CO5300 over QSPI). Anything
  drawn outside the inscribed circle is hidden by the bezel — design
  accordingly.
- **Inputs**: 2 physical buttons (`M5.BtnA`, `M5.BtnB`) + CST820B
  capacitive touch on the whole face.
- **Sensors**: BMI270 6-axis IMU (accel + gyro) on internal I²C.
- **IO expander**: M5IOE1 on internal I²C @ `0x4F`. Pin 8 = display
  power (driven by M5Unified), pin 9 = vibration motor PWM.
- **Storage**: PSRAM 8 MB; 2 MB littlefs region (`/badge/slot_0..3.bin`,
  466×466 RGB565 LE each = 434,472 bytes).

## 2. Reference image

User-supplied reference is Apple-Watch-style dark-theme cards:
- Dark / near-black background
- Cards with **large rounded corners** (radius ≈ 24 px in their grid;
  scale to ~12 px for our 466-px face)
- One hero number / word per card, large weight
- Neon-cyan / neon-red accents (we'll soften this — see §6)
- Small icon top-left of each card
- Card-internal CTA button ("Buy Now", "+ Add") in a saturated colour

The cards are square / rectangular in the reference but **we have a
round display** — the rounded corners of the visible face naturally
clip anything outside the inscribed circle. Don't fight the bezel,
lean into it.

## 3. Target app set (final)

The user explicitly wants **only three apps** — drop VoiceToy and
Compass permanently. VoiceToy's I²S bring-up races the OLED power-on
sequence and was rejected; Compass was deemed redundant with Balance.

| App        | Purpose                                                    |
| ---------- | ---------------------------------------------------------- |
| Stopwatch  | Start / stop / lap / reset millisecond timer.             |
| Balance    | Bubble level for table-flatness / surface tilt.            |
| Badge      | Slideshow of user-uploaded images (LittleFS).             |

Launcher shows three "cards" in a circular arrangement; swipe or
KEYA/KEYB cycles between them; tap to enter.

## 4. UI / UX requirements (verbatim from user)

- **Flat / modern** look — no neon glow, no shader, no arcade vibe.
  The existing cyberpunk neon palette is being retired.
- **Circular layout** — cards arranged around the disc, centre card
  is the focused app, side cards hint at the previous / next.
- **Swipe to switch apps** in the launcher.
- **Two physical buttons** (`KEYA`, `KEYB`) are the **only** input in
  the Stopwatch app. They must be obvious: the UI should show small
  bottom-of-screen hints that read `KEYA` / `KEYB` with their action
  (`Start` / `Stop`, `Lap` / `Reset`).
- **Badge app**: forward / backward image navigation via the same
  two physical buttons. Tap to pause / resume.

## 5. Build & library configuration (the four things that must be
   right or the panel stays black)

### 5a. Arduino FQBN (must be the full string — CLI does not apply
IDE defaults)

```
m5stack:esp32:m5stack_stopwatch:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default
```

Note `PSRAM=opi` — without it `M5GFX.cpp:1640-1643` skips the OLED
power-on block.

### 5b. `partitions.csv` (this is THE bug that ate the previous
session — must have `app0` at `0x10000`, NOT `factory` at `0x20000`)

```
nvs,        data, nvs,       0x009000,  0x006000
otadata,    data, ota,       0x00F000,  0x002000
app0,       app,  ota_0,     0x010000,  0x300000
app1,       app,  ota_1,     0x310000,  0x300000
littlefs,   data, littlefs,  0x610000,  0x9E0000
coredump,   data, coredump,  0xFF0000,  0x001000
```

Bootloader looks for `app0` at `0x10000`. `factory` at `0x20000` is a
PlatformIO convention that does **not** apply on Arduino.

### 5c. `src/ioexpander.cpp` must use `&M5.In_I2C` (not `&Wire`)

```cpp
auto err = expander.begin(&M5.In_I2C);
```

The M5IOE1 chip is on internal I²C (pins 47/48). Using `&Wire`
(default external I²C, pins 11/10) gives SDA=255 SCL=255 and the init
silently fails. The BMI270 IMU is on the same internal bus and uses
`M5.In_I2C` for its own auto-detect — when our `ioexpander.begin(&Wire)`
fails, the IMU still works (because M5Unified has already opened
In_I2C), but vibration motor and any other M5IOE1-controlled IO
silently fail.

### 5d. `LittleFS.begin()` must pass the partition label

```cpp
LittleFS.begin(true /* formatOnFail */, "/littlefs", 10, "littlefs");
```

Otherwise the lib defaults to looking for a partition named `spiffs`,
which our table doesn't have.

## 6. Palette — start fresh

The previous neon palette (`CYAN_NEON`, `MAGENTA_NEON`, etc.) is being
retired. For the new flat UI, use:

| Token       | RGB565  | Use                                       |
| ----------- | ------- | ----------------------------------------- |
| `BG`        | `0x0000` | app background (true black)              |
| `SURFACE`   | `0x1A1A` | card surface, dark grey                  |
| `SURFACE_2` | `0x2A2A` | card hover / selected                    |
| `TEXT_HI`   | `0xFFFF` | primary text (white)                     |
| `TEXT_LO`   | `0xB0B0` | secondary text                           |
| `ACCENT`    | `0x5BD0` | accent (calm cyan, not neon)             |
| `ACCENT_2`  | `0xFF8A4D` | warm accent (peach)                     |
| `DANGER`    | `0xF05050` | warning / imbalance / record dot        |

**All 16-bit RGB565 — no 24-bit literals.** The compiler silently
truncates `0xFF2BD6` etc., so don't ever declare palette as
`uint32_t` or `int`.

## 7. Current source layout (for reference / partial reuse)

```
include/
  app.h              App base class + input::Event types (keep)
  app_stopwatch.h    StopwatchApp declaration (will rewrite draw())
  app_balance.h      BalanceApp declaration (will rewrite draw())
  app_badge.h        BadgeApp declaration (will rewrite draw())
  shell.h            launcher shell public API (rewrite significantly)
  theme.h            palette + widget primitives (rewrite)
  ioexpander.h       single M5IOE1 instance for vibration (KEEP)
src/
  app_stopwatch.cpp  rewrite the draw()
  app_balance.cpp    rewrite the draw()
  app_badge.cpp      rewrite the draw()
  shell.cpp          rewrite the launcher (3-app swipe, no carousel)
  theme.cpp          rewrite the primitives (rounded card, hint label,
                     button hint)
  ioexpander.cpp     KEEP — already correct (`&M5.In_I2C`)
stopwatch_multiverse.ino
                     thin: M5.begin(cfg) → shell::begin() → loop
                     cfg.internal_imu = true is required
                     (Balance + the launcher doesn't need mic/spk)
```

**Drop these** entirely:
- `app_voicetoy.cpp` / `app_voicetoy.h` — I²S bring-up races OLED.
- `app_compass.cpp` / `app_compass.h` — user wants only 3 apps.
- Any reference to `M5PM1` or `M5IOE1` outside of `ioexpander.cpp` —
  M5Unified handles these internally.

## 8. App UI sketches (text mockups — describe to the new context)

### Launcher
```
         .─────────.
       /  Stopwatch  \         ← focused card, large rounded rect,
      |  HH:MM:SS.mmm  |          centred, ~280×280 px
      |  [tap to open]  |
       \             /
         '───────'
   ← badge     balance →       ← small hints left & right (KEYA/KEYB
                                 or swipe direction)
   1/3                          ← page indicator
```

### Stopwatch (entered by tap)
```
   STOPWATCH
   ┌──────────────────────┐
   │                      │
   │     00:01:23.456     │   ← giant mono-digit counter
   │                      │
   │   L1  0:13.228       │   ← last lap (small)
   │                      │
   │  [KEYA]  [KEYB]      │   ← button hints at bottom
   │  start   lap        │
   └──────────────────────┘
```
States: Idle, Running, Stopped. KEYA toggles start/stop. KEYB =
lap when running, reset when stopped. The button hints swap labels
based on state.

### Balance (entered by tap)
```
   BALANCE
        ╭─────────╮
       /           \
      |     ●      |        ← bubble at centre when flat
       \           /
        ╰─────────╯
       2.5°  BALANCED      ← angle + status
   ─────────────────
        tilt the watch
```
The dial is a circle inside a circle (the watch face is already a
circle). Bubble moves from centre based on gravity vector projection.
Text under the dial shows degrees and state (BALANCED / UNBALANCED).
On UNBALANCED, the dial border turns warm accent and the vibration
motor pulses at 2 Hz.

### Badge (entered by tap)
```
   ● ○ ○ ○          ← 4 dots, current slot lit
   ┌──────────────────────┐
   │                      │
   │     [image]          │   ← 466×466 RGB565 pushed as-is
   │                      │
   │                      │
   │                      │
   └──────────────────────┘
   [KEYA] prev   [KEYB] next
```
Tap to pause / resume auto-cycle. KEYA = previous slot, KEYB = next
slot. When no images uploaded, show centred text "NO IMAGES" + a
small "Upload via tools/badge_uploader" hint.

## 9. Serial image upload protocol (unchanged — reuse it)

Already implemented in `shell.cpp`'s `pollSerial()` and `app_badge.cpp`:

- `LIST\n` → `SLOTS <size> <size> <size> <size>\n` (4 slots, 0 if empty
  else 434472)
- `BIMG <slot> <size>\n<raw 466×466 RGB565 bytes>` → store to
  `/badge/slot_<slot>.bin`, reply `OK\n`
- `ERASE <slot>\n` → delete the file, reply `OK\n`

The host tool is at `../tools/badge_uploader/` (Python web app).

## 10. Build command (for the new context — copy-paste)

```bash
arduino-cli --config-file ~/.arduinoIDE/arduino-cli.yaml \
  compile \
  --fqbn "m5stack:esp32:m5stack_stopwatch:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default" \
  --upload --port COM4 \
  /path/to/stopwatch_multiverse/stopwatch_multiverse.ino
```

Add `EraseFlash=all` to the FQBN above to do a full chip wipe
(needed when changing partitions, harmless otherwise).

## 11. Diagnostic shortcuts (for the new context)

If the panel ever goes black again:
1. Open serial at 115200 baud, look for
   `E (25) boot: No bootable app partitions` → partition bug (§5b).
2. `M5StopWatch need OPI-PSRAM enabled` → FQBN missing `PSRAM=opi`
   (§5a).
3. `SDA=255, SCL=255` from M5IOE1 → forgot `&M5.In_I2C` (§5c).
4. `partition "spiffs" could not be found` → forgot the `"littlefs"`
   label in `LittleFS.begin()` (§5d).

Full debug chain in `~/.claude/projects/D--Dev/memory/`.

## 12. Known minor issues (carry-over)

- The Balance app's `sample()` did not set `_imuReady = true` until
  the very last fix. Make sure your rewrite of Balance flips that
  flag on the first successful `M5.Imu.getAccel()` so the "IMU
  OFFLINE" toast doesn't get stuck on.
- Vibration motor is on M5IOE1 PWM channel 9 (per
  `ioexpander::setVibrationDuty`).
- `app_badge.cpp` allocates a 466×466×2 framebuffer in PSRAM via
  `ps_malloc`. If you drop that and use a smaller intermediate
  buffer, push the image in chunks.