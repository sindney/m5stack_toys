# Tasks — `m5-stopwatch-multiverse` (rewrite)

This file captures the work remaining for the three-app flat-UI
rewrite on the M5Stack C152 StopWatch. Items already verified in the
working tree (`stopwatch_multiverse/`) are checked; the rest of the
list is what `opsx:apply` will drive.

## 1. Repo & toolchain

- [x] 1.1 Create `stopwatch_multiverse/` sketch tree with
      `stopwatch_multiverse.ino`, `src/`, `include/`, `scripts/`,
      `docs/`.
- [x] 1.2 Ship `partitions.csv` alongside the sketch with `app0` at
      `0x10000` and a `littlefs` data partition ending at `0xFF0000`.
- [x] 1.3 Pin ESP32 Arduino core to `espressif32 @ 6.12.0` and
      `m5stack:esp32:m5stack_stopwatch` FQBN (PSRAM=opi) in
      `platform.txt`.
- [x] 1.4 Record the vendored libraries (`M5Unified`, `M5GFX`,
      `M5IOE1`) in `libraries.txt`. `M5PM1` is intentionally **not**
      listed — M5Unified manages the battery PMU.

## 2. Flat UI theme

- [x] 2.1 Rewrite `include/theme.h` with the eight palette constants
      `BG` / `SURFACE` / `SURFACE_2` / `TEXT_HI` / `TEXT_LO` /
      `ACCENT` / `ACCENT_2` / `DANGER`, all `uint16_t` RGB565, plus
      the small / large font constants.
- [ ] 2.2 Replace `drawRingSelector` with `drawCard(x, y, w, h,
      radius, fill, border)` in `src/theme.cpp`.
- [ ] 2.3 Add `drawHint(cx, cy, label, secondary)` for the centred
      bottom-of-screen hint text (e.g. "BALANCED / 0.0°").
- [ ] 2.4 Add `drawButtonHint(cx, cy, key, label)` that renders the
      `[KEYA] start [KEYB] lap` footer style.
- [x] 2.5 Keep `perfMode` + `frameReady()` from the cyberpunk
      primitive (cheap, useful for the bubble animation).

## 3. App shell & lifecycle

- [x] 3.1 Keep the `App` base class (`onEnter`, `onTick`, `onInput`,
      `onExit`) in `include/app.h`. Drop the `VoiceToy` entry from
      `AppId`; `Compass` stays removed.
- [ ] 3.2 Rewrite the launcher draw in `src/shell.cpp` to the
      three-card circular layout (focused card centre, two side
      hints, page indicator).
- [ ] 3.3 Wire KEYA / KEYB to nav and detect double-press within
      350 ms (same as before — keep the working code path).
- [x] 3.4 Wire CST820B touch with 60 ms debounce and 8 px bezel
      deadzone (existing).
- [ ] 3.5 Add swipe-left / swipe-right gesture detection (≥ 80 px
      travel, ≥ 200 ms) that cycles focus without firing a tap.
- [ ] 3.6 Register the three apps in `g_apps[]` in the order
      Stopwatch, Balance, Badge. Update the `AppId` enum
      accordingly.
- [ ] 3.7 Verify the shell returns to launcher on double-press from
      each app.

## 4. Stopwatch app

- [x] 4.1 Keep `esp_timer_get_time()` based timing with 10 ms
      precision (existing).
- [x] 4.2 Render elapsed time in `HH:MM:SS.mmm` (existing).
- [x] 4.3 Keep KEYA = start / stop toggle, KEYB = lap (running) /
      reset (stopped). Tap = start / stop toggle.
- [ ] 4.4 Rewrite `draw()` to the handoff layout: large mono-digit
      counter centred, last lap below, [KEYA] / [KEYB] button hint
      footer whose labels swap by state.

## 5. Badge app

- [x] 5.1 Keep LittleFS mount under `littlefs:/badge/` (existing).
- [x] 5.2 Keep the 434 472-byte raw RGB565 loader (existing).
- [x] 5.3 Auto-advance every 5 s (existing).
- [x] 5.4 Keep KEYA = prev, KEYB = next, tap = pause / resume.
- [ ] 5.5 Rewrite `draw()` to the handoff layout: 4 slot dots (one
      lit) at the top, image pushed as-is (or "NO IMAGES" centred if
      no slots are valid), [KEYA] prev [KEYB] next button hint
      footer.

## 6. Balance app

- [x] 6.1 Keep the ≥ 50 Hz accelerometer read and tilt-vs-+Z
      calculation (existing).
- [x] 6.2 Keep the 2° balanced envelope (existing).
- [x] 6.3 Keep the 2 Hz vibration pulse on UNBALANCED via M5IOE1
      PWM channel 9.
- [ ] 6.4 Rewrite `draw()` to the handoff layout: bubble inside an
      inner circle (the watch face is already a circle), degrees +
      BALANCED / UNBALANCED text under the dial, "tilt the watch"
      hint footer.
- [ ] 6.5 Fix `_imuReady` so it flips on the *first* successful
      `M5.Imu.getAccel()`, not on the last call. (Carry-over from the
      previous session; the "IMU OFFLINE" toast was getting stuck on
      until the app was restarted.)

## 7. Drop Compass + VoiceToy

- [ ] 7.1 Delete `include/app_compass.h` and `src/app_compass.cpp`.
- [ ] 7.2 Remove `app_compass.h` includes and the `CompassApp`
      static instance + array entry from `stopwatch_multiverse.ino`.
- [x] 7.3 VoiceToy is already absent from the source — drop the
      stale `AppId::VoiceToy = 4` enum value from `include/app.h`
      and confirm no references remain.
- [ ] 7.4 Remove `M5PM1` references from `libraries.txt` and any
      `#include "M5PM1.h"` lines (verify none exist).

## 8. Build & flash script

- [x] 8.1 `scripts/flash.sh` idempotently installs the ESP32 core
      and the three M5 libs.
- [x] 8.2 `scripts/flash.sh` compiles with the full `m5stack:esp32:
      m5stack_stopwatch:…PSRAM=opi,…,PartitionScheme=app3M_fat9M_16MB`
      FQBN and `arduino-cli upload --port <auto-detected>`.
- [ ] 8.3 `scripts/flash.sh` should not overwrite flash for a
      same-config rebuild — confirm `--upload` keeps the existing
      LittleFS contents when the partition table is unchanged.

## 9. Documentation

- [x] 9.1 `README.md` at `stopwatch_multiverse/` covers build, flash,
      and badge upload.
- [ ] 9.2 Document the flat theme palette and the three primitives in
      `docs/THEME.md` so future app authors know which primitive to
      reach for.
- [x] 9.3 The serial protocol (`LIST`, `BIMG`, `ERASE`) is documented
      in the badge app spec and in `tools/badge_uploader/README.md`.

## 10. On-device build & smoke tests

These run on the real device. The change is **not** complete until
every app is exercised on hardware without crashing.

- [ ] 10.1 `arduino-cli compile` against the FQBN succeeds with no
      errors and no unexpected warnings.
- [ ] 10.2 `arduino-cli board list` shows the StopWatch's USB-CDC
      port; if it does not auto-detect, list candidates and pass
      `--port COM*` explicitly.
- [ ] 10.3 If `arduino-cli upload` times out, ask the user to put the
      device into download mode (hold reset ~2 s until the green LED
      lights, per StopWatch docs) and retry.
- [ ] 10.4 Burn the firmware and confirm the AMOLED shows the
      launcher within 3 s of reboot.
- [ ] 10.5 Smoke-test the **launcher**: KEYA cycles left, KEYB
      cycles right, swipe gestures cycle, focus wraps at both ends,
      every card renders without clipping.
- [ ] 10.6 Smoke-test **Stopwatch**: start → wait 5 s → stop → tap
      lap → reset via KEYB when stopped → verify the counter reads
      `00:00:05.0xx` ±10 ms and the last lap is shown.
- [ ] 10.7 Smoke-test **Badge**: upload one image via the host
      uploader → confirm slot count reply → verify the image
      renders full-screen → tap to pause → KEYB advances →
      double-press KEYA returns to launcher.
- [ ] 10.8 Smoke-test **Balance**: place the watch flat and confirm
      "BALANCED" → tilt past 2° and confirm vibration pulses at ~2
      Hz → return to flat and confirm vibration stops within 500 ms.
- [ ] 10.9 Soak test: leave the launcher (and each app for 30 s) on
      battery power for 5 minutes; confirm no crash, no AMOLED
      tearing, no I²C errors in serial log.
- [ ] 10.10 If any verification step fails, file a follow-up task
      under a new section and re-run the affected smoke test before
      marking this section complete.
