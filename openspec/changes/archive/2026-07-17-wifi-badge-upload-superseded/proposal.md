> **SUPERSEDED 2026-07-17** — never smoke-tested on device (tasks 6.x/7.x
> were never checked off). The Wi-Fi AP transport kicked the host PC off
> the internet for every upload, the firmware still wiped badge slots on
> every boot, and 32 slots never fit the LittleFS partition. Replaced by
> `changes/badge-usb-drive` (USB mass-storage + HTML host tool). Kept for
> reference only; do not implement.

## Why

The current USB-CDC transport for badge uploads (`badge-uploader push`,
Tkinter GUI) is failing on the M5Stack StopWatch in this environment:
the firmware's `Serial` output never reaches COM4 even after a clean
re-flash with the right FQBN, and the device's screen stays lit while
the chip is in download mode (PID `303A:1001`). Wi-Fi sidesteps the
broken USB path, is materially faster (434 KB over Wi-Fi 802.11n
typically finishes in well under a second vs. ~30 s on USB-CDC at
115 200 baud), and untethers the watch from the USB cable during
upload.

## What Changes

- **BREAKING** Drop USB-CDC transport from `badge-uploader` entirely.
  The `--port` / `--serial-port` flags go away. There is no serial
  fallback. The whole `protocol.py` (ASCII-over-serial) and
  `ports.py` (VID/PID auto-detect) modules are removed.
- **BREAKING** Add a 4th app `WiFi Upload` to the
  `stopwatch_multiverse` launcher. The new app is the only path to
  push badge images from the host. The existing Stopwatch / Balance
  / Badge apps are unchanged.
- **New** `stopwatch_multiverse/src/app_wifi_upload.cpp` runs the
  watch in Wi-Fi AP mode (`StopWatch-XXXX`, open network) and
  serves a tiny HTTP API on `192.168.4.1`:
  `GET /api/slots`, `POST /api/upload?slot=N` (raw RGB565 LE body),
  `POST /api/erase?slot=N`. The slot file layout
  (`/badge/slot_<N>.bin`) is unchanged.
- **New** `tools/badge_uploader/badge_uploader/wifi.py` — HTTP
  client that mirrors `protocol.upload_image` /
  `protocol.erase_slot` against the watch's endpoints.
- `cli.py` takes `--host` / `-H` (default `192.168.4.1`) instead of
  `--port`. `ui.py` opens with a "Host" entry pre-filled with
  `192.168.4.1` and a "Connect" button; status label shows the
  watch's reported slot count after connecting.
- README rewrites the "Run" section to point users at the watch's
  Wi-Fi app, the AP SSID, and the `192.168.4.1` default.
- `pyproject.toml` adds `requests>=2.31` (replaces pyserial).

## Capabilities

### New Capabilities

- `badge-wifi-ap`: The firmware 4th app — Wi-Fi AP, HTTP server,
  AMOLED status display, slot I/O against the existing
  `/badge/slot_<N>.bin` littlefs layout.
- `badge-wifi-upload`: The host transport — HTTP client in
  `wifi.py`, CLI and GUI surface in `cli.py` / `ui.py` taking
  `--host`. No USB-CDC code path.

### Modified Capabilities

- *(none — `openspec/specs/` is empty; nothing to delta against.)*
  The conceptual predecessors (`badge-cli-upload`,
  `badge-desktop-uploader`, `badge-image-uploader`) live only in
  `openspec/changes/archive/2026-07-06-badge-upload-simplify/`. This
  change is a fresh start under Wi-Fi transport names.

## Impact

- **Firmware code added**:
  `stopwatch_multiverse/src/app_wifi_upload.cpp`,
  `stopwatch_multiverse/include/app_wifi_upload.h`. The `.ino` adds
  the new app to the `g_apps[]` table.
- **Firmware code unchanged**: `app_stopwatch.cpp`, `app_balance.cpp`,
  `app_badge.cpp`, `shell.cpp`, `theme.cpp`, `ioexpander.cpp`. The
  slot file layout is unchanged.
- **Host code added**: `tools/badge_uploader/badge_uploader/wifi.py`.
- **Host code removed**: `tools/badge_uploader/badge_uploader/server.py`
  (already removed in `badge-upload-simplify`),
  `tools/badge_uploader/badge_uploader/protocol.py`,
  `tools/badge_uploader/badge_uploader/ports.py`.
- **Host code modified**: `cli.py`, `ui.py`, `dispatch.py`, `__init__.py`,
  `pyproject.toml`, `README.md`.
- **Dependencies**: drop `pyserial>=3.5`; add `requests>=2.31`. Pillow
  and Tkinter unchanged.
- **Out of scope**: BLE / Wi-Fi station-mode provisioning, TLS, an
  HTTP-based reverse-direction fetch (watch → host), OTA firmware
  update.
