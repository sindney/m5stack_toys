## Context

Two upload transports failed in a row. USB-CDC under `USBMode=hwcdc`
never delivered serial data to the host in this environment. The Wi-Fi
SoftAP replacement (`archive/2026-07-17-wifi-badge-upload-superseded`)
was reasonable code but the wrong choice: every upload required joining
the watch's open AP (host loses internet), it was never smoke-tested,
and the firmware around it carried three latent bugs — a boot-time wipe
of all badge slots, a `MAX_SLOTS=32` that exceeded the 9.9 MB LittleFS
partition, and no persistence story.

The user's mental model is the phone model: plug in USB, device shows
up as a file volume, drop files. The ESP32-S3's native USB OTG plus the
M5Stack core's TinyUSB stack deliver exactly that via the Mass Storage
Class — no drivers on any desktop OS, no pairing, no network.

Verified facts behind the design (checked on disk, not assumed):

- `cores/esp32/USBMSC.h` + `CONFIG_TINYUSB_MSC_ENABLED=y` in
  `tools/esp32s3-libs/3.3.7-cn/sdkconfig`.
- `boards.txt` exposes `m5stack_stopwatch.menu.USBMode.default`
  (USB-OTG TinyUSB) — switchable from the FQBN, no board file edits.
- `ARDUINO_USB_ON_BOOT` is never defined by this platform, so
  `USB.begin()` inside `setup()` is the first and only `tinyusb_init()`
  — interfaces registered by global constructors (`Serial`'s USBCDC and
  our static `USBMSC`) are both present in the single composite
  descriptor.
- IDF 5.5's `esp_vfs_fat_spiflash_mount_ro` mounts raw FAT read-only
  with no wear levelling — the layout FatFS parses is byte-identical to
  the LBAs the host writes over MSC.
- IDF 5.5's `diskio_rawflash` is read-only by design
  (`ff_raw_write()` returns `RES_WRPRT`), so `f_mkfs` through it fails
  with `FR_WRITE_PROTECTED`. First-boot format therefore uses a custom
  writable diskio driver (40 lines) backed by the same RMW helper as
  the MSC write path.

## Goals / Non-Goals

**Goals:**
- Plug the watch in, open the USB Drive app, get a `BADGE` drive on the
  PC in under ~2 s. Copy `slot_XX.bin` (00–15) onto it from Explorer or
  from `badge_uploader.html`. Eject, exit, watch the images in Badge.
- Host tool with zero install: one HTML file, Chrome/Edge File System
  Access API writes straight to the drive; `.bin` download fallback
  everywhere else.
- Debug console back: TinyUSB CDC carries boot logs on the COM port.
- Images persist across reboots; slot count is honest (16 × 434 312 B
  = 6.9 MB in a 9.9 MB partition).
- Self-healing storage: first boot (or a corrupted volume) formats
  FAT16 automatically.

**Non-Goals:**
- BLE / Wi-Fi transports of any kind, MTP, OTA.
- Watch-side PNG/JPEG decoding (host always pre-encodes RGB565 LE).
- Wear levelling / power-loss-safe writes from the device side (the
  device never writes the partition; host writes are rare).
- Simultaneous host+device filesystem access (prevented by the
  invariant below).

## Decisions

### MSC, not MTP or serial
MSC is a class driver on every desktop OS — plug and play, drive
letter, drag-and-drop, and writable from a browser via the File System
Access API. MTP would need a custom TinyUSB class plus Windows quirks
for no UX gain. A serial protocol would need a host-side program again
(that's the two failures we just buried).

### Raw partition + read-only device mount
The host is the only writer. The device mounts
`esp_vfs_fat_spiflash_mount_ro` — no wear levelling — so the bytes it
parses are exactly the bytes the host wrote. Concurrent access is
excluded by one invariant:

    device VFS mounted  <=>  MSC media absent

`UsbDriveApp.onEnter` unmounts then presents; `onExit` hides then
remounts. The MSC layer answers reads/writes at the raw-block level and
never touches the VFS.

### MSC writes are read-modify-erase-write
NOR flash programs 1→0 only; a 512-byte host write must not destroy the
other seven 512-byte blocks sharing its 4 KB flash sector.
`usbdrive::rawWrite` reads the 4 KB sector, merges, erases, rewrites.
Slow in theory, irrelevant in practice (a full 434 KB slot is ~1 s).

### FAT16 "super-floppy", formatted on-device
`f_mkfs(FM_FAT | FM_SFD)` — no MBR, LBA0 is the boot sector, which is
exactly what the MSC layer exposes (the whole partition as the disk).
Windows mounts it as a removable drive; label `BADGE`. Slot names stay
8.3-safe (`SLOT_XX.BIN`) so no LFN concerns anywhere.

### Composite CDC+MSC from boot, media gated by the app
The card reader is always enumerated (with the CDC console); only the
*media* is gated (`mediaPresent(false)` at boot). Entering the USB
Drive app "inserts the card", exiting "removes" it. No USB stack
restart, no re-enumeration races, and the debug console works in every
app — the thing the old agents never had.

### HTML page replaces the Python package
The Python CLI/Tkinter stack needed pip installs and duplicated the
compositing math. The browser gives us canvas compositing, drag/pan/zoom
UX, and direct file writes to the drive in one self-contained file.
The RGB565 pack is a direct port of the old `encode.py`
(fit-then-zoom, centre+pan, 1.5 px feather, premultiply-vs-black via
canvas compositing), unit-tested for exact size (434 312 B) and byte
order.

### 1200 bps touch for reflashing
Under TinyUSB the esptool DTR/RTS auto-reset is unreliable here; the
core reboots into download mode when the CDC port is opened at 1200
baud (`USBCDC.cpp` `_bit_rate == 1200 → usb_persist_restart`). flash.sh
documents it; pyserial one-liner does it.

### File byte order vs M5GFX pushImage (found in field testing)
Slot files are RGB565 **little-endian** — but `pushImage(uint16_t*)`
interprets its buffer as `swap565_t` (big-endian, raw blit) because
`_swapBytes` defaults to false (`LGFXBase::create_pc(const uint16_t*)`).
Feeding LE bytes to it byte-scrambles every pixel (the "thermal"
portrait symptom). The Badge app casts the framebuffer to
`const lgfx::rgb565_t*`, which selects the converting path. The file
format stays LE; only the interpretation call changed — images written
before the fix display correctly after it.

## Risks / Trade-offs

- [Host write caching after copy] → The AMOLED tells the user to eject;
  `onStartStop` latches the SCSI eject and the app shows "EJECTED — safe
  to exit". Remount on exit re-reads everything from scratch.
- [Flash wear without wear levelling] → Slot writes are rare (a badge
  refresh is a few sector erases). Fine for the product's life.
- [User picks the wrong folder in the HTML tool] → Cosmetic: files land
  where they picked; the page auto-descends into a `badge` subfolder
  when one exists.
- [Windows "Format disk?" prompt on a corrupted volume] → The device
  formats at boot before the host can see media, so this only appears
  if the user somehow presents media on a volume the device itself
  failed to format (logged on CDC).
- [Power loss mid host-write] → FAT may be inconsistent; device-side
  mount fails next boot and the formatter recovers (data loss confined
  to the badge partition, which is replaceable by definition).

## Migration Plan

1. Firmware: land partitions.csv + usb_drive + badge_fs + UsbDriveApp +
   BadgeApp/shell/.ino changes; flash via the 1200 bps touch.
2. Host: delete `tools/badge_uploader/`; add `tools/badge_uploader.html`
   + `tools/fixtures/test_slot.bin`.
3. Verify (done on hardware): composite enumerates; CDC logs flow;
   BADGE drive mounts; Explorer copy → device read-back byte-identical
   (FNV-1a `9b164a79`); file survives hard reboot.
4. Remaining user-assisted checks: USB Drive app tile via buttons,
   Badge slideshow visuals, HTML page click-through.
5. Rollback: reflash the archived Wi-Fi build; the badgeimg partition
   is independent, so old slot files are untouched either way (they're
   in different partition layouts — LittleFS content is gone, by
   design of this migration).
