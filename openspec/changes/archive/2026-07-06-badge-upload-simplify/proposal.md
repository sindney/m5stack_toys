## Why

The current `tools/badge_uploader/` tool drives uploads through a Flask
HTTP server (`server.py`) that has multiple design problems on Windows:

1. `app.run(host=..., port=..., debug=True)` enables the Werkzeug
   reloader, which forks a child process. The serial handle is opened in
   the parent *and* reused by the child after the reloader kicks in.
   Subsequent requests hit a process holding a duplicate USB-CDC handle
   and the child cannot actually talk to the watch — uploads silently
   succeed from the browser's point of view but no bytes reach the
   device, or the child crashes and the parent re-opens the port
   mid-transfer.
2. Even with `debug=False`, every upload round-trips the PNG through
   the browser → HTTP → Flask → pyserial. Three extra hops where any
   one of them dropping the connection shows up as "upload failed
   after retries" in the user's log.
3. The HTML carries slot management (`+ new`, the slot dropdown, the
   slot-bar showing sizes) that belongs on the device side; the host
   only needs **pick image, position, upload**.

This change swaps the Flask-based tool for a Python-native UI that talks
USB serial directly. There is no HTTP server, no reloader, no PNG
round-trip, and slot management is left to a future firmware UX.

## What Changes

- **BREAKING** Replace `tools/badge_uploader/server.py` (Flask) with
  `tools/badge_uploader/ui.py` (Tkinter-based native window) using
  Pillow's `ImageTk` to render a 466×466 circular preview directly on
  the Python side — no browser involved.
- Keep `badge_uploader/encode.py`, `badge_uploader/protocol.py`,
  `badge_uploader/ports.py` unchanged. They are already decoupled from
  the transport and remain the right shape for a native client.
- Add a small native dialogue: pick an image file (via `tkinter.filedialog`),
  pan with click-and-drag over a `Canvas`, zoom with mouse-wheel or
  +/- buttons, "Upload to slot N" (slot chosen via a Spinbox, default 0).
- Add a CLI fallback path for headless / scripted use:
  `badge-uploader push --slot N --image foo.png [--pan-x 0 --pan-y 0 --zoom 1.0]`.
  It does not open a window; it composites, encodes, and sends.
- Keep the `pyproject.toml` `dependencies` list but drop Flask. Add a
  console-script entry that dispatches to either the GUI or the CLI
  based on the first positional argument (`ui` vs `push`).
- Delete `tools/badge_uploader/static/` and
  `tools/badge_uploader/templates/` (no longer served).
- Update `tools/badge_uploader/README.md` with the new commands.
- Do **not** modify the firmware's serial handler or `BadgeApp` —
  `LIST` / `BIMG` / `ERASE` are unaffected. Slot UX on the device is a
  separate concern; this change explicitly defers it.

## Capabilities

### New Capabilities

- `badge-desktop-uploader`: A native Tkinter window that lets a user
  pick an image, position it under a circular mask, preview the result,
  and push the encoded RGB565 blob to a chosen slot over USB-CDC.
- `badge-cli-upload`: A subcommand (`badge-uploader push`) that
  composites and uploads in one step for scripting / CI.

### Modified Capabilities

- `badge-image-uploader`: The HTTP-server requirement is replaced with
  the native-UI requirement. The serial-protocol requirement, the
  circular-conversion requirement, and the auto-detection requirement
  are kept. The HTML cropping UI requirement (drag/zoom on a `<canvas>`
  in the browser) is **removed**.

## Impact

- **Affected code**:
  `tools/badge_uploader/badge_uploader/server.py` (deleted),
  `tools/badge_uploader/badge_uploader/__init__.py` (dispatch entry),
  `tools/badge_uploader/pyproject.toml` (drop Flask, new script entry),
  `tools/badge_uploader/README.md` (new commands).
- **New code**: `tools/badge_uploader/badge_uploader/ui.py`,
  `tools/badge_uploader/badge_uploader/cli.py`.
- **Untouched**: `tools/badge_uploader/badge_uploader/{encode,protocol,ports}.py`,
  `stopwatch_multiverse/src/app_badge.cpp`,
  `stopwatch_multiverse/src/shell.cpp` (the device-side serial handler).
- **Dependencies**: drop `Flask>=2.3`; keep `Pillow>=10.0`,
  `pyserial>=3.5`. Tkinter ships with the stdlib on the Python.org
  Windows installer used on this machine, so no new dependency.
- **Out of scope**: BLE transport, Wi-Fi provisioning, on-device slot
  management, OTA updates, mobile companion app.
