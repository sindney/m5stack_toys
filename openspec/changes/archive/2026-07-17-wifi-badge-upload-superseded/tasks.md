## 1. Drop the serial transport from the host

- [x] 1.1 Edit `tools/badge_uploader/pyproject.toml` — drop
      `pyserial>=3.5`; add `requests>=2.31`. Bump version to
      `0.3.0`. The `badge-uploader` script entry stays
      `badge_uploader.dispatch:main`.
- [x] 1.2 Delete `tools/badge_uploader/badge_uploader/protocol.py`
      and `tools/badge_uploader/badge_uploader/ports.py`. (Their
      last shipped version is the one archived under
      `openspec/changes/archive/2026-07-06-badge-upload-simplify/`;
      git history keeps them.)
- [x] 1.3 Run `python -c "import badge_uploader; import
      badge_uploader.encode; import badge_uploader.wifi; import
      badge_uploader.ui; import badge_uploader.cli; import
      badge_uploader.dispatch"` after step 2 lands to confirm
      there are no leftover `pyserial` or `serial` imports in the
      host tree.

## 2. Host: HTTP transport

- [x] 2.1 Add `tools/badge_uploader/badge_uploader/wifi.py` with
      `list_slots(host, retries=5, backoff=1.0) -> SlotInfo`,
      `upload_image(host, slot, rgb565_le, timeout=30) -> None`,
      and `erase_slot(host, slot) -> None` against
      `http://<host>/api/slots`, `…/api/upload?slot=N`, and
      `…/api/erase?slot=N`. The `SlotInfo` dataclass stays in
      `encode.py` (or moves to a new `types.py` — decide based on
      what reads cleanest). Raise `WifiError(reason)` on any
      non-2xx response; the CLI maps that to exit code 3.
- [x] 2.2 Update `tools/badge_uploader/badge_uploader/cli.py`:
      replace `--port`/`-p` and `--baud` with `--host`/`-H`
      (default `192.168.4.1`). On `push`, encode with
      `encode.composite_to_rgb565`, call
      `wifi.upload_image(args.host, slot, rgb565)`. Print
      `Uploaded slot N (434472 bytes)` on success, map exit codes
      2/3/4 per the spec.
- [x] 2.3 Update `tools/badge_uploader/badge_uploader/ui.py`:
      add a Host `Entry` (pre-filled `192.168.4.1`) and a
      "Connect" button next to the existing controls. Connect
      calls `wifi.list_slots(host)` and shows
      `N occupied, 32-N empty` in the status label. Replace
      `_open_serial` / `_close_serial` with a thin `wifi.*` call
      in the Upload handler. The "Pick image…", drag-pan, zoom,
      Spinbox, and 466×466 canvas are unchanged.
- [x] 2.4 `tools/badge_uploader/badge_uploader/dispatch.py`
      stays as-is (no argv change).
- [x] 2.5 `tools/badge_uploader/badge_uploader/__init__.py`
      stays as-is.

## 3. Update docs and packaging

- [x] 3.1 `tools/badge_uploader/README.md`: rewrite the "Run"
      section. Drop the "open 127.0.0.1:5000" / `--port` /
      `--serial-port` content. Add:
        * "On the watch, open the WiFi Upload app. The AMOLED
          shows the AP SSID `StopWatch-XXXX` and the IP
          `192.168.4.1`."
        * "On the host, `pip install -e .` then either
          `badge-uploader` (GUI) or
          `badge-uploader push -s 0 -i FOO.png` (CLI, default
          `--host 192.168.4.1`)."
        * A "Breaking change" note that the USB-CDC transport
          and the `--port` / `--serial-port` flags are gone.
- [x] 3.2 Verify the protocol table in the README now reads HTTP
      endpoints, not the `LIST` / `BIMG` / `ERASE` ASCII rows.

## 4. Verify the host side

- [x] 4.1 Reinstall (`pip install -e .`) from
      `tools/badge_uploader/`. Confirm
      `pip show badge-uploader` lists `Pillow, requests` and not
      `pyserial`.
- [x] 4.2 Run `badge-uploader --help` and
      `badge-uploader push --help` — confirm the new
      `--host`/`-H` flag is present and `--port` is gone.
- [x] 4.3 Run `python -c "import badge_uploader, import
      badge_uploader.wifi, import badge_uploader.ui, import
      badge_uploader.cli, import badge_uploader.encode,
      import badge_uploader.dispatch"` — confirm all imports
      succeed and no module references `pyserial` or `serial`.

## 5. Firmware: 4th app (WiFi Upload)

- [x] 5.1 Add `stopwatch_multiverse/include/app_wifi_upload.h`
      declaring `class WifiUploadApp : public App { void begin();
      void end(); void tick(); }`.
- [x] 5.2 Add `stopwatch_multiverse/src/app_wifi_upload.cpp`:
      SoftAP bringup in `begin()` — SSID `StopWatch-XXXX`
      (last 4 hex of MAC), open, channel 6, 20 MHz, max TX
      power; DHCP on `192.168.4.0/24`; gateway
      `192.168.4.1`. The `end()` tears the AP and HTTP server
      down. `tick()` polls the M5 buttons (KEYA hold to retry,
      KEYA+KEYB to exit) and the HTTP server.
- [x] 5.3 In `app_wifi_upload.cpp`, mount the HTTP server on
      port 80 with handlers for `GET /`, `GET /api/slots`,
      `POST /api/upload?slot=N`, `POST /api/erase?slot=N`.
      Validate `Content-Length == IMG_SIZE` for upload; on
      mismatch, reply `400 bad_size`. Slot range 0..31; out of
      range replies `400 slot_range`. Successful write replies
      `200 OK`. Erase of a missing slot still replies `200 OK`.
- [x] 5.4 In `app_wifi_upload.cpp`, render the AMOLED status
      display: AP SSID, IP, slot count, "Uploading…" while a
      transfer is in progress, and a 3 s "Uploaded slot N"
      banner on success.
- [x] 5.5 In `stopwatch_multiverse.ino`, instantiate a
      `WifiUploadApp g_wifi;` and add `&g_wifi` to the
      `g_apps[]` table. The launcher already iterates the table
      and shows tiles; no launcher change needed beyond the
      array entry.
- [x] 5.6 Build with the existing
      `m5stack:esp32:m5stack_stopwatch:...` FQBN (drop
      `USBMode=hwcdc,CDCOnBoot=cdc` — Wi-Fi is the new
      transport; USB-CDC is no longer needed). Run
      `arduino-cli compile` and confirm 0 errors.

## 6. Flash and smoke-test on the watch

- [x] 6.1 Run `arduino-cli compile … --upload --port COM4
      stopwatch_multiverse.ino`. Watch for the upload to
      complete and the chip to reset.
- [ ] 6.2 On the watch, navigate the launcher to the new
      "WiFi Upload" tile (KEYA cycles, KEYB confirms). Confirm
      the AMOLED shows `WiFi: StopWatch-XXXX` and
      `IP: 192.168.4.1` within 3 s.
- [ ] 6.3 From the host, run
      `curl http://192.168.4.1/api/slots` and confirm the JSON
      response lists 32 sizes (zeros for empty slots).
- [ ] 6.4 Run `badge-uploader push -s 0 -i
      tests/fixtures/sample.png --host 192.168.4.1`. Confirm
      the CLI prints `Uploaded slot 0 (434472 bytes)` and the
      AMOLED shows `Uploaded slot 0` for 3 s.
- [ ] 6.5 Open the Badge app on the watch. Confirm slot 0
      appears in the slideshow within 5 s.
- [ ] 6.6 Repeat 6.4 with `-s 3` and confirm `GET /api/slots`
      now shows two non-zero entries.

## 7. Verify the GUI path

- [ ] 7.1 Run `badge-uploader`. Confirm the window opens with
      a Host field pre-filled with `192.168.4.1`.
- [ ] 7.2 Click "Connect". Confirm the status line shows
      `2 occupied, 30 empty` (or whatever the watch reports).
- [ ] 7.3 Pick an image, drag, click Upload. Confirm the
      status line shows `Uploaded slot N` and the watch's
      Badge slideshow updates.
- [ ] 7.4 Stop the watch's Wi-Fi Upload app, then click
      "Connect" in the GUI. Confirm the status line shows
      `Watch not reachable`.
