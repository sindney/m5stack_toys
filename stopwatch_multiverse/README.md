# stopwatch_multiverse

Four-app launcher firmware for the [M5Stack StopWatch (C152)](https://docs.m5stack.com/en/hat/stopwatch) — a round 466×466 AMOLED wearable with two programmable buttons (KEYA / KEYB), a CST820B touch controller, a BMI270 6-axis IMU, an M5IOE1 vibration motor, and an M5Unified-managed battery.

## What you get

A single Arduino sketch that boots straight into the **Badge** app (change `g_bootApp` in `stopwatch_multiverse.ino` to pick another app or the launcher) and dispatches to **Stopwatch**, **Balance**, **Badge**, and **USB Drive**. Clicking KeyA / KeyB cycles apps and triggers in-app actions; a screen tap (or pressing A+B together) enters the focused app; pressing A+B inside an app pops back to the launcher.

| App       | What it does                                                                |
| --------- | --------------------------------------------------------------------------- |
| Stopwatch | start / stop / lap / reset with millisecond precision, KEYA / KEYB hints    |
| Balance   | Bubble level with haptic feedback (vibration motor pulses at 2 Hz off-flat) |
| Badge     | Slideshow of circular images copied onto the watch over USB mass storage    |
| USB Drive | Presents the badge partition to a PC as a removable drive (MSC)             |

All apps share one flat dark UI theme (`include/theme.h`) — the Matrix-green RGB565 palette and the same primitives everywhere.

## Hardware

* Board: **M5Stack StopWatch (C152)**, ESP32-S3R8 with CO5300 AMOLED + CST820B touch.
* Pins and bus assignments are handled by **M5Unified** — the sketch does not touch them directly.
* Partition layout: see `partitions.csv` — `app0 @ 0x10000`, `app1 @ 0x310000`, and a ~9.9 MB raw FAT data partition `badgeimg @ 0x610000`.

## Toolchain

```
arduino-cli lib install M5Unified M5GFX M5IOE1
```

Uses the `m5stack:esp32` core (3.3.x). The exact FQBN is pinned in
`scripts/flash.sh`:
`m5stack:esp32:m5stack_stopwatch:…,USBMode=default,CDCOnBoot=cdc,…,PSRAM=opi,…`.

`PSRAM=opi` is mandatory — without it the `M5GFX` power-on block for the
CO5300 display is skipped and the AMOLED stays black.

`USBMode=default` selects the ESP32-S3's TinyUSB (USB-OTG) stack. The
firmware enumerates as a composite device: a CDC serial console (debug
logs on the COM port) plus an MSC card reader backed by the raw
`badgeimg` flash partition. The card reader shows **no media** until the
USB Drive app presents it.

## Build and flash

From this directory:

```bash
scripts/flash.sh                 # build + upload
scripts/flash.sh --no-upload     # build only
```

The running app reboots into download mode when its CDC port is opened
at **1200 baud** (the standard "1200 bps touch"): open the port at 1200,
close, wait ~3 s, then re-run the script. If that fails, hold the
StopWatch's reset button ~2 seconds until the green LED lights
(download mode) and re-run.

## Badge image workflow

1. On the watch, open the **USB Drive** app — a **BADGE** removable
   drive appears on the PC within a couple of seconds. (First boot after
   flashing formats the partition automatically.)
2. On the PC, open `tools/badge_uploader.html` in Chrome/Edge:
   connect the drive, drop an image, drag to pan, wheel to zoom, pick a
   slot, write. Or skip the page entirely and drag any pre-encoded
   `slot_XX.bin` (466×466 RGB565 LE, 434 312 bytes, X = 00–15) onto the
   drive in Explorer.
3. Eject the drive in Windows, exit the app on the watch (press A+B),
   open **Badge** — the slideshow picks the new images up fullscreen
   (auto-advance every 5 s, tap to pause, click KEYA/KEYB to step
   manually).

Images persist across reboots — they live in their own flash partition,
which firmware flashing never touches.

## Layout

```
stopwatch_multiverse/
├── stopwatch_multiverse.ino   # sketch entry, app registry (4 apps)
├── partitions.csv             # partition table (app0 @ 0x10000, badgeimg FAT @ 0x610000)
├── libraries.txt              # required Arduino libraries (no M5PM1)
├── docs/
│   ├── REWRITE-HANDOFF.md     # the spec this firmware was rewritten from
│   └── THEME.md               # palette + primitive docs
├── include/                   # public headers
│   ├── app.h                  #   App base class + input event types
│   ├── shell.h                #   launcher shell public API
│   ├── theme.h                #   flat dark palette + widget primitives
│   ├── badge_fs.h             #   read-only FAT mount of badgeimg (+ first-boot format)
│   ├── usb_drive.h            #   TinyUSB CDC+MSC composite over raw badgeimg
│   ├── app_stopwatch.h        #   per-app headers
│   ├── app_balance.h
│   ├── app_badge.h
│   ├── app_usb_drive.h
│   └── ioexpander.h           #   single M5IOE1 instance for the vibrator
├── src/                       # implementations
│   ├── shell.cpp              #   launcher, card draw, click/combo buttons, touch
│   ├── badge_fs.cpp
│   ├── usb_drive.cpp
│   └── …                      #   per-app implementations
├── scripts/
│   └── flash.sh               # one-command build + upload
└── tools/
    ├── badge_uploader.html    # host-side image tool (single file, zero install)
    └── fixtures/test_slot.bin # pre-encoded 434 312-byte test image
```

## License

MIT, like the rest of this repo — see `../LICENSE`.
