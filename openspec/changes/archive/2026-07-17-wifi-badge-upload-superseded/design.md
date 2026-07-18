## Context

`badge-uploader` (host) and the `stopwatch_multiverse` firmware have a
broken USB-CDC link in this environment: even with the right FQBN
(`USBMode=hwcdc,CDCOnBoot=cdc`) and a clean re-flash, the watch's
Serial output never reaches COM4 on the host. The M5Stack core maps
`Serial` to `HWCDCSerial` correctly under this FQBN, so the bug is on
the firmware's side (likely a boot-time hang past `M5.begin()`'s
display init), not the host transport. Either way, USB-CDC is
unusable here today.

Wi-Fi as the upload transport sidesteps USB-CDC entirely. The M5Stack
StopWatch (C152) is an ESP32-S3R8 with the full Wi-Fi 4 (802.11 b/g/n)
radio on board. The PSRAM is 8 MB OPI. Power budget is fine: the user
is plugging the watch in (or charging it) for the few seconds the
upload takes, so AP-mode power draw is irrelevant.

The user picked **Open AP, Wi-Fi only** out of three options. Open AP
means no password handshake, simplest code, fastest to ship. Wi-Fi
only means the `protocol.py` / `ports.py` serial stack goes away
entirely; there's no fallback.

`encode.py` (compositing + RGB565) and the slot file layout on the
device (`/badge/slot_<N>.bin`) are unchanged. The transport is the
only thing moving.

## Goals / Non-Goals

**Goals:**
- Watch creates a 2.4 GHz open AP `StopWatch-XXXX` (last 4 hex of MAC)
  on entering the Wi-Fi Upload app. SoftAP default IP `192.168.4.1`.
- Watch serves a tiny HTTP API on port 80 with three endpoints
  (`/api/slots`, `/api/upload`, `/api/erase`) and a status page
  (`/`) for the user's browser.
- Host CLI: `badge-uploader push -s N -i FOO.png --host 192.168.4.1`
  with `--host` defaulting to `192.168.4.1` and `--port` for HTTP
  going away entirely.
- Host GUI: "Host" entry pre-filled with `192.168.4.1`, a "Connect"
  button, and the existing pick / drag / zoom / upload controls
  unchanged. Slot Spinbox unchanged.
- Same RGB565 LE payload (434 472 bytes) on the wire — only the
  transport changes.
- Bootstrap time: AP up + HTTP server listening in under 3 s of
  entering the app.

**Non-Goals:**
- Wi-Fi station-mode provisioning (typing the home SSID/PSK on the
  watch). AP-only for this change.
- TLS / mDNS discovery / service pairing. Plain HTTP, host IP
  defaults to the well-known SoftAP gateway.
- BLE / WebSocket transports. Wi-Fi only.
- Mobile companion app or browser-side upload UI on the watch's
  status page (the `/` page is for debugging only).
- Multi-slot concurrent uploads. Serialise.
- New `encode.py` features — the visual preview in the GUI keeps
  using the existing `composite_to_rgb565` against a fake canvas.

## Decisions

### Open AP, no password
The user picked open. Cost: a phone sitting in range during the
upload can see the bytes (they're just a slot of RGB565, not a
secret). Mitigation: the AP is up for the duration of the upload
only; the user exits the app to take it down. An open network also
saves a PSK prompt on hosts that have already joined the network
once — the OS reconnects automatically. (A WPA2-PSK option can be
added later behind a build flag; not now.)

### `192.168.4.1` as the default SoftAP gateway
ESP-IDF's default SoftAP configuration puts the device at
`192.168.4.1` on `192.168.4.0/24`. Hard-coding this in the CLI/GUI
default keeps the docs short: "open the Wi-Fi Upload app on the
watch, then run `badge-uploader push -s 0 -i foo.png`."

### HTTP, not raw TCP
Two reasons to prefer HTTP over a custom TCP protocol:
- `requests` on the host and `WebServer` on the ESP32-S3 are
  well-trodden. No new protocol to debug, no framing / chunking
  code on the firmware side, no retry policy to design.
- The 30 s USB-CDC retry policy from `protocol.upload_image` is
  unnecessary over Wi-Fi: a TCP retransmit on the order of
  milliseconds is enough. The CLI/GUI's failure path becomes "any
  non-2xx response is a hard error", which is dramatically
  simpler.

The cost: HTTP overhead for 434 KB of binary is small (one header
per request, no per-byte framing). Wi-Fi 802.11n at 150 Mbit/s link
rate finishes the transfer in well under a second even with
overhead.

### Three endpoints, not a single binary pipe
| Endpoint | Method | Body | Response |
| --- | --- | --- | --- |
| `/api/slots` | GET | – | `200 {"sizes":[n0,n1,...]}` (one int per slot) |
| `/api/upload?slot=N` | POST | raw RGB565 LE bytes | `200 OK` or `400/4xx/5xx <reason>` |
| `/api/erase?slot=N` | POST | – | `200 OK` or `4xx <reason>` |

Rationale: keep each request idempotent / small. The host can list
slots without committing to a write, and the firmware can validate
the slot range and size *before* it tries to write to littlefs.
This mirrors the existing `LIST` / `BIMG` / `ERASE` separation, just
over HTTP.

### Watch returns a slot count, not the full `/badge/` directory
`LIST` in the serial protocol returned the byte length of every slot
file. The HTTP version returns the same thing: an array of lengths
in slot order. The host's `wifi.list_slots` parses it the same way
`protocol.list_slots` parses the `SLOTS n0 n1 n2 n3` line.

### Encoding unchanged
The host encodes once with `encode.composite_to_rgb565` and POSTs
the resulting 434 472 bytes. The firmware writes them straight into
`/badge/slot_<N>.bin`. The slot size matches `IMG_SIZE` exactly; any
mismatch is rejected with `400 bad_size` (analogous to the existing
`ERR bad_size` reply on the serial protocol).

### GUI reuses Tk + Pillow
The new Host field sits where the old "Pick serial port" combobox
would have been. The pre-fill is `192.168.4.1`; "Connect" calls
`wifi.list_slots` and populates a status line with the count
("3 occupied, 29 empty"). Upload is unchanged: encode, POST, show
result. We do NOT re-render the preview on connect — the preview
still needs an image picked, same as before.

### `requests` over `urllib`
`requests` has connection pooling, retries on the underlying
`urllib3`, and a clean streaming API for the 434 KB upload. On the
firmware side `WebServer` (ESP-IDF / `WebServer.h` from the Arduino
core) is the simplest thing that works. The two libraries do not
need to be feature-compatible, only speak the same wire protocol.

## Risks / Trade-offs

- [Open AP visible to anyone nearby during upload] → Mitigation: the
  app is up only while the user is in the Wi-Fi Upload app; the
  payload is a slot of RGB565 (not a secret). A 30 s window is fine.
- [SoftAP channel / range issues] → Mitigation: pin channel 6 (most
  common 2.4 GHz AP default; least interference from Wi-Fi 6E 6 GHz
  gear), 20 MHz bandwidth, max TX power (the 2.4 GHz antenna is on
  the PCB). Document "stay within ~5 m" in the README.
- [Concurrent uploads racing on the same slot] → Mitigation: the
  firmware serialises writes by holding a per-slot mutex during
  `/api/upload`. The second request blocks for the first to finish
  or times out at 30 s. Single-host usage is the norm; this is
  belt-and-braces.
- [HTTP body parse failure on the firmware] → Mitigation: the
  firmware reads `Content-Length` from the request, reads exactly
  that many bytes, validates `Content-Length == IMG_SIZE`, and
  replies `400 bad_size` otherwise. No chunked encoding.
- [Wi-Fi init fails (e.g. antenna not connected)] → Mitigation: the
  app displays `Wi-Fi init failed: <err>` on the AMOLED and
  disables the "Ready" indicator. The user can hold A to retry.
- [Power draw on the watch during upload] → Mitigation: AP draws
  ~150 mA extra on the ESP32-S3; the watch is plugged in or being
  held, the upload takes <2 s, and the watch goes back to the
  launcher when the user exits the app.
- [Host tries to connect before the AP is up] → Mitigation: the CLI
  / GUI retries the `GET /api/slots` health-check 5 times with 1 s
  backoff before reporting "watch not reachable". The watch
  typically has the AP up within 2 s of entering the app.

## Migration Plan

1. Land the firmware change (`app_wifi_upload.cpp` + `.ino` table
   entry) on a branch; flash via the existing `flash.sh`.
2. Land the host change: remove `protocol.py` and `ports.py`, add
   `wifi.py`, update `cli.py` / `ui.py` / `dispatch.py` /
   `pyproject.toml` / `README.md`.
3. Reinstall (`pip install -e .`) and verify `badge-uploader --help`
   shows the new `--host` flag.
4. Smoke-test: enter the Wi-Fi Upload app on the watch, confirm the
   AP SSID appears and the IP is `192.168.4.1`. On the host, run
   `badge-uploader push -s 0 -i tests/fixtures/sample.png`. Verify
   `LIST` returns slot 0 with 434 472 bytes and the badge app's
   slideshow shows the new image.
5. README rewrite — drop the USB-CDC / serial-port section,
   document the Wi-Fi workflow, link the M5Stack StopWatch docs.
6. Rollback: the `protocol.py` / `ports.py` modules live in git
   history (commit `badge-upload-simplify`); revert = `git checkout`.

No on-device data migration. The slot file layout
(`/badge/slot_<N>.bin`) is unchanged. Existing slots keep working.
