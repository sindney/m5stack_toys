---
schema: spec-driven
created: 2026-07-17
---

## Why

Badge image upload has been the project's flaky spot through two
transports: USB-CDC (serial output never reached the host under
`USBMode=hwcdc`; agents flew blind) and Wi-Fi SoftAP + HTTP (never
smoke-tested; every upload kicked the host PC off the internet; the
firmware still wiped all badge slots on boot; `MAX_SLOTS=32` × 434 KB
never fit the 9.9 MB LittleFS partition).

The M5Stack StopWatch's ESP32-S3 has native USB OTG, and the M5Stack
core 3.3.7 ships TinyUSB with `CONFIG_TINYUSB_MSC_ENABLED=y` — so the
watch can present itself as a USB mass-storage device, the same
"plug in, drag files" model a phone or card reader uses. Verified on
hardware: composite CDC+MSC enumerates on Windows 11, the CDC console
delivers boot logs, and a host-written slot file reads back from the
device byte-identical (FNV-1a match over 434 312 bytes).

## What Changes

- **BREAKING** The Wi-Fi Upload app, its HTTP API, the legacy
  LIST/BIMG/ERASE serial protocol, and the Python `badge_uploader`
  package (CLI + Tkinter GUI + `wifi.py`) are all removed. There is no
  network transport of any kind.
- **New** `USB Drive` app (4th launcher tile): presents the raw
  `badgeimg` flash partition over TinyUSB MSC while active, hides it on
  exit. The host PC writes `slot_XX.bin` files directly — by Explorer
  drag-and-drop or via the new HTML tool.
- **New** `partitions.csv`: the LittleFS partition is replaced by a
  ~9.9 MB `badgeimg` (data/fat) partition at 0x610000. Badge slots are
  the only files on it; images now persist across reboots (the old
  boot-wipe is gone).
- **New** `badge_fs` module: read-only FAT mount at `/badge`
  (`esp_vfs_fat_spiflash_mount_ro` — no wear levelling; the host is the
  only writer), plus one-time first-boot FAT16 format via a custom
  writable diskio driver (IDF's `diskio_rawflash` is read-only and
  cannot `f_mkfs`).
- **New** `usb_drive` module: TinyUSB composite (CDC console + MSC card
  reader). MSC `onWrite` is read-modify-erase-write per 4 KB flash
  sector — required on raw NOR flash.
- `BadgeApp` reads `/badge/slot_XX.bin` (zero-padded, 8.3-safe) via
  POSIX `fopen/fread`; `MAX_SLOTS` 32 → 16 (6.9 MB, fits honestly).
- **New** `tools/badge_uploader.html`: single self-contained page
  (no install) that composites an image onto the 466×466 circular face
  (pan/zoom, feathered mask, RGB565 LE, exactly 434 312 bytes) and
  writes it onto the BADGE drive via the File System Access API, with a
  `.bin` download fallback. Replaces the Python package.
- FQBN switches `USBMode=hwcdc` → `USBMode=default` (TinyUSB OTG);
  `CDCOnBoot=cdc` kept. Reflashing uses the 1200 bps touch to enter
  download mode.

## Capabilities

### New Capabilities

- `badge-usb-msc`: Firmware USB mass-storage transport — `usb_drive`
  (TinyUSB composite, raw partition, RMW writes, eject tracking),
  `badge_fs` (ro mount + first-boot format), the `USB Drive` app, and
  the `badgeimg` partition layout.
- `badge-html-uploader`: Host tool — the single-file HTML page with
  image compositing, slot grid with occupied detection, direct-to-drive
  writes via File System Access API, erase, and download fallback.

### Modified Capabilities

- *(none — `openspec/specs/` is empty; nothing to delta against.)*
  Predecessors live only in `openspec/changes/archive/`
  (`2026-07-06-badge-upload-simplify`, and
  `2026-07-17-wifi-badge-upload-superseded` which this change replaces).

## Impact

- **Firmware added**: `include/usb_drive.h`, `src/usb_drive.cpp`,
  `include/badge_fs.h`, `src/badge_fs.cpp`, `include/app_usb_drive.h`,
  `src/app_usb_drive.cpp`.
- **Firmware modified**: `stopwatch_multiverse.ino` (app table, boot
  sequence, LittleFS/boot-wipe removal), `partitions.csv`,
  `include/app.h` (`AppId::UsbDrive`), `include/app_badge.h` +
  `src/app_badge.cpp` (FAT POSIX reads, 16 slots, no serial handlers),
  `include/shell.h` + `src/shell.cpp` (serial protocol removed),
  `scripts/flash.sh` (TinyUSB FQBN, uploader launch removed),
  `README.md`.
- **Firmware deleted**: `src/app_wifi_upload.cpp`,
  `include/app_wifi_upload.h`, `docs/SERIAL_PROTOCOL.md`,
  `scripts/probe_serial.py`.
- **Host added**: `tools/badge_uploader.html`,
  `tools/fixtures/test_slot.bin` (pre-encoded 434 312-byte test image).
- **Host deleted**: `tools/badge_uploader/` (entire Python package).
- **Dependencies**: none on the host (browser only). Firmware adds no
  Arduino libraries (TinyUSB/FatFS ship in the core).
- **Out of scope**: BLE, Wi-Fi (any mode), MTP, OTA, watch-side image
  decoding (host always pre-encodes RGB565), wear levelling (host-side
  writes are rare).
