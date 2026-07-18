## Context

`tools/badge_uploader/` currently moves images host → device through
a Flask web app on `127.0.0.1:5000`. The user reports uploads are
broken and suspects the HTTP-server path. The fingerprint of the bug is
that `app.run(host=..., port=..., debug=True)` enables the Werkzeug
reloader, which forks a child process and keeps the parent's serial
handle open while the child takes over request handling. The child can
write the `BIMG ...` header to its inherited USB-CDC FD, but the
host-side state machine never sees the response, the parent never
re-opens the port, and the user sees "Upload failed after retries"
from `protocol.upload_image`'s retry counter without anything reaching
the watch.

The non-debug path is also fragile on Windows: each request goes
browser → HTTP → Flask → pyserial, three processes sharing one
USB-CDC handle, and any of them losing it shows up as a silent upload
failure. The HTML also carries slot-management UX (`+ new`, slot
dropdown, size readout) that the user wants off this tool.

The user prefers a Python-native upload path with a minimal "pick +
crop + upload" window — and is happy to defer slot management to the
device. Confirmed toolkit: **Tkinter** (stdlib on Windows Python).
Confirmed scope: **single Spinbox for slot N**, no list / `+new` /
`Pick latest`. The CLI subcommand should ship alongside the GUI for
scripting.

The existing `encode.py`, `protocol.py`, `ports.py` are transport-free
and stay; only the transport layer (server.py + Flask) and the
HTML/JS/CSS assets are replaced.

## Goals / Non-Goals

**Goals:**
- One Python process opens the serial port, encodes, and writes the
  bytes directly to the device — no HTTP, no reloader, no PNG
  round-trip.
- Same UX clarity as the HTML version: 466×466 circular preview, drag
  to pan, wheel to zoom, slot Spinbox, one Upload button.
- A second CLI surface (`badge-uploader push -s N -i FOO.png`) for
  headless / scripted use.
- `encode.py` / `protocol.py` / `ports.py` unchanged — the protocol
  is already correct, the only thing broken was the transport.
- `pyproject.toml` deps: drop `Flask`; keep `Pillow`, `pyserial`.

**Non-Goals:**
- BLE / Wi-Fi transport (BLE would require a NimBLE GATT service on
  the firmware plus a custom host BLE client, and 434 KB over BLE
  notify is ~minutes even at the 2-MTU maximum).
- Slot list, slot allocation, or any device-side UX (deferred to the
  watch's `BadgeApp`).
- Cross-platform packaging (the user is on Windows; Tkinter and
  pyserial are already available in the Python.org installer that they
  use).
- Touch / pinch-zoom (desktop mouse only, like the original HTML).
- Pre-flight view of the round-trip — the original "send PNG back"
  preview was a roundabout way to confirm the encode; the live Tkinter
  preview is sufficient.

## Decisions

### Tkinter over PySide6 / PyWebView
PySide6 gives a nicer look and QGraphicsView has free pan/zoom
gestures, but adds ~80 MB of dependencies for a tool that just opens
once per upload. PyWebView preserves the existing HTML/CSS/JS but
pulls in a Chromium runtime to host them — the user explicitly asked
not to need a browser step. Tkinter ships with the stdlib on the
Python.org Windows installer used on this machine, the existing
Pillow skill set covers rendering, and the user picked it.

The trade-off: Tkinter `Canvas` is more imperative than
`QGraphicsView`. Mitigations:
- Use a single `PhotoImage` per drag-and-release cycle (not per frame)
  to keep redraws cheap.
- The mask is drawn on top of the canvas as a `create_oval` outline +
  a `Canvas` background. We do NOT cut the image to a circle on the
  Tkinter side; we just preview the rectangular crop and trust
  `encode.composite_to_rgb565` to mask it correctly. The original HTML
  had the same shape (it drew the rectangle inside the `#stage` and
  trusted the host-side encode).

### Single upload path: serial only, no HTTP
Replacing Flask with `python -m http.server` would still need a
browser step and a server PID. Tkinter's event loop runs in the same
process as the serial write, so the upload button can call
`protocol.upload_image(ser, slot, rgb565)` and show the result inline.
This is the structural fix the user asked for ("if you can make
similar ux with py directly do so is fine").

Alternatives considered:
- **BLE**: out — requires firmware changes and is slow.
- **WebUSB direct from the browser**: possible, but locks us back into
  the Chrome-only WebUSB model and the user has said they prefer Python.
- **Raw `COM7` shell command**: works but has no preview; the user
  wants drag/zoom.

### Drop slot-management UX from the host
The `+ new` button allocates the next free slot number from the local
list. With the device already doing the same job via `refreshSlotMap()`
(the badge app scans `/badge/slot_*.bin` at startup and after every
serial callback), the host's list is redundant. The desktop uploader
becomes `Slot Spinbox → Upload`; the CLI takes `--slot N`.

### Keep `upload_image`'s retry semantics
`protocol.upload_image` already retries up to 3 times on transient
`ERR` replies because of USB-CDC buffer glitches. The Tkinter window
uses the same function and surfaces failures in a status label rather
than swallowing them.

### Process model
Tkinter's event loop (`root.mainloop()`) blocks the main thread. The
serial write is called directly from the Upload button's callback, so
it blocks the UI while the 434 KB transfer completes (typically <2 s at
115 200 baud even with retries). That is acceptable for a tool used a
few times per session. If it later becomes annoying, the right fix is
to push the encode and the protocol call onto a `threading.Thread`
and post results back via `root.after(0, ...)` — but that adds
threading complexity that is not justified today.

## Risks / Trade-offs

- [GUI hangs during upload] → Mitigation: the upload button disables
  itself for the duration; status line shows "Uploading…". Keep the
  freeze <5 s by reusing the 1024-byte chunked write in
  `upload_image`.
- [User accidentally overwrites a non-empty slot] → Mitigation: the
  Spinbox does not warn. Add an optional `--confirm-overwrite` CLI
  flag and a confirmation popup in the GUI if the slot exists on the
  device (`LIST` once at startup). Mark as follow-up if the user asks.
- [Tkinter looks dated on HiDPI Windows] → Mitigation: leave DPI
  handling to the default; the canvas is fixed 466×466 anyway.
- [Dropping Flask breaks any local scripts that POSTed to
  `127.0.0.1:5000/upload`] → Mitigation: this repo only calls the
  uploader from the GUI / CLI; no other scripts touch it. README will
  mention the breaking change.

## Migration Plan

1. Add `tools/badge_uploader/badge_uploader/ui.py` and `cli.py` next
   to the existing `server.py`.
2. Add the new entry point to `pyproject.toml`
   (`badge-uploader = "badge_uploader.dispatch:main"`) and remove the
   Flask one.
3. Delete `static/`, `templates/`, and `server.py` once `ui.py` /
   `cli.py` land and the README is updated.
4. Reinstall via `pip install -e .` and verify `badge-uploader` opens
   the Tk window; `badge-uploader push --image ...` uploads to slot 0.

No rollback strategy needed — the previous Flask version lives in
git; revert = `git checkout` of the renamed files.

## Open Questions

- Should the GUI also pull `LIST` once at startup purely to pick a
  better default slot (next free)? Punted — the user said slot UX
  belongs on the device.
- Is the user OK with the UI freezing for ~2 s per upload, or do we
  want a worker thread? Default: freeze. Trivial to add later if
  anyone complains.
