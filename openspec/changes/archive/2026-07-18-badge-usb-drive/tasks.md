## 1. Storage foundation

- [x] 1.1 `partitions.csv`: replace `littlefs` with
      `badgeimg, data, fat, 0x610000, 0x9E0000`; app0 stays at 0x10000.
- [x] 1.2 `src/usb_drive.cpp` + `include/usb_drive.h`: TinyUSB
      composite — static `USBMSC` (registers its interface at
      construction, before the single `USB.begin()` in setup), raw
      `badgeimg` partition `onRead`/`onWrite`, RMW per 4 KB sector in
      `rawWrite`, `onStartStop` eject latch, `mediaPresent(false)` at
      boot, shared `rawRead`/`rawWrite` helpers.
- [x] 1.3 `src/badge_fs.cpp` + `include/badge_fs.h`:
      `esp_vfs_fat_spiflash_mount_ro("/badge")`, first-boot FAT16 SFD
      format via a custom writable diskio driver + `f_mkfs`, label
      `BADGE`, unmount/remount with the mounted<=>media-absent
      invariant.
- [x] 1.4 `.ino`: drop LittleFS mount/mkdir/boot-wipe; boot order
      theme → badgefs → usbdrive → shell.

## 2. Firmware apps

- [x] 2.1 `src/app_usb_drive.cpp` + `include/app_usb_drive.h`: 4th
      launcher tile; onEnter counts slots → unmount → present media;
      onTick redraws on host eject; onExit hides media → remount.
      AMOLED shows state, slot usage, eject hint.
- [x] 2.2 `app_badge.{h,cpp}`: POSIX `fopen/fread` from `/badge`,
      zero-padded `slot_%02d.bin`, `MAX_SLOTS` 32→16, serial
      trampolines removed.
- [x] 2.3 `shell.{h,cpp}`: LIST/BIMG/ERASE protocol, `uploading()`,
      `registerBadgeHandler` removed — pure launcher.
- [x] 2.4 Delete `app_wifi_upload.{h,cpp}`, `docs/SERIAL_PROTOCOL.md`,
      `scripts/probe_serial.py`. `AppId::Wifi` → `AppId::UsbDrive`.

## 3. Host tool

- [x] 3.1 `tools/badge_uploader.html`: single file; image pick/drop,
      pan/zoom composite to 466×466 (circle mask, 1.5 px feather,
      premultiply vs black), RGB565 LE encode validated at 434 312 B.
- [x] 3.2 Slot grid 0–15 with occupied detection (name + size),
      `showDirectoryPicker` connect (auto-descends into `badge/`),
      write via `createWritable`, erase via `removeEntry`, `.bin`
      download fallback.
- [x] 3.3 Delete `tools/badge_uploader/` (Python package). Add
      `tools/fixtures/test_slot.bin` (pre-encoded gradient).
- [x] 3.4 JS syntax check (node --check) and encoder unit test
      (size + RGB565 LE byte order) — pass.

## 4. Build & flash plumbing

- [x] 4.1 `scripts/flash.sh`: FQBN `USBMode=default,CDCOnBoot=cdc`;
      Python uploader launch removed; 1200 bps touch documented as the
      download-mode entry.
- [x] 4.2 Compile 0 errors/warnings; flash to hardware.

## 5. Hardware verification (done)

- [x] 5.1 Composite enumerates: COM port + "USB Mass Storage" node;
      CDC boot logs flow.
- [x] 5.2 Boot-time format produces the BADGE FAT16 volume; media
      present → drive letter in ~2 s.
- [x] 5.3 Explorer copy of `test_slot.bin` → device reads back
      434 312 B, FNV-1a `9b164a79` both sides.
- [x] 5.4 File survives a full hard reboot (no boot wipe).

## 6. User-assisted verification (remaining)

- [x] 6.1 On the watch: launcher → USB DRIVE tile → BADGE drive appears
      on the PC; exit (hold A+B) → drive disappears.
- [x] 6.2 `badge_uploader.html` in Chrome: connect drive → slot grid
      shows slot 0 occupied; drop a photo, write slot 1; erase slot 1.
- [x] 6.3 Badge app shows the written image in the slideshow; it still
      shows after a watch reboot. (Required the `rgb565_t*` cast fix —
      byte-order scramble found here.)
- [x] 6.4 Eject-in-Windows → watch shows "EJECTED — safe to exit".

## 7. Bookkeeping

- [x] 7.1 `wifi-badge-upload` moved to
      `archive/2026-07-17-wifi-badge-upload-superseded/` with a note.
- [x] 7.2 README.md rewritten for the USB-drive workflow.
- [x] 7.3 Memory notes updated (USB PID, FQBN, MSC/diskio gotchas).
