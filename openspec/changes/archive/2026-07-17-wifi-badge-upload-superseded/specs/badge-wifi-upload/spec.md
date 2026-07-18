## ADDED Requirements

### Requirement: Headless push over Wi-Fi
The host uploader SHALL provide a `push` subcommand that loads an
image, composites it under the circular mask, encodes RGB565
little-endian, and POSTs the bytes to the watch's HTTP API — all
without opening a Tkinter window.

#### Scenario: Default invocation
- **WHEN** the user runs
  `badge-uploader push --slot 0 --image ./avatar.png`
- **THEN** the tool reads the image, composites at pan=(0,0),
  zoom=1.0, encodes 434 472 bytes, POSTs the body to
  `http://192.168.4.1/api/upload?slot=0`, waits for `200 OK`,
  prints `Uploaded slot 0 (434472 bytes)` to stdout, and exits
  with status 0.

#### Scenario: Custom host
- **WHEN** the user runs
  `badge-uploader push --slot 2 --image p.png --host 192.168.4.1`
- **THEN** the request goes to the explicit host instead of the
  default. The HTTP port is always 80.

#### Scenario: Custom pan and zoom
- **WHEN** the user runs
  `badge-uploader push --slot 2 --image p.png --pan-x -10 --pan-y 5 --zoom 1.2`
- **THEN** the image is composited at pan=(-10, +5), zoom=1.2
  before encoding, and is sent to slot 2.

#### Scenario: Missing image
- **WHEN** the user runs
  `badge-uploader push --slot 0 --image nope.png`
  and `nope.png` does not exist
- **THEN** the tool prints `image not found: nope.png` to stderr
  and exits with status 2 (no HTTP request is made).

#### Scenario: Watch unreachable
- **WHEN** the watch's HTTP server is not reachable after 5
  attempts (1 s backoff each)
- **THEN** the tool prints `watch not reachable at <host>:80` to
  stderr and exits with status 4.

#### Scenario: Device rejected upload
- **WHEN** the watch replies `400 bad_size` (or any 4xx/5xx)
- **THEN** the tool prints `device rejected upload: <reason>` to
  stderr and exits with status 3.

### Requirement: Slot picker, no management
The CLI uploader SHALL take the slot number as a `--slot` argument
(`-s` short form) with default 0. It SHALL NOT query, list, or
auto-allocate slot numbers; that lives on the device.

#### Scenario: Default slot
- **WHEN** the user runs `badge-uploader push --image foo.png`
- **THEN** the slot defaults to 0 and the push targets slot 0.

### Requirement: Tkinter upload window
The host uploader SHALL provide a native Tkinter window that lets
the user pick an image, position it under a circular mask, preview
the result, and push the encoded RGB565 blob to the watch over
Wi-Fi.

#### Scenario: Window opens with the default host
- **WHEN** the user runs `badge-uploader` (or `badge-uploader ui`)
- **THEN** a Tkinter window appears with: a Host entry pre-filled
  with `192.168.4.1`, a "Connect" button, a "Pick image…" button,
  a 466×466 square preview canvas with a circular mask, a Zoom
  slider, a Slot Spinbox defaulting to 0, and an "Upload" button.

#### Scenario: Connect reports slot count
- **WHEN** the user clicks "Connect" with the watch's Wi-Fi Upload
  app open
- **THEN** the host issues `GET /api/slots`, parses the JSON
  response, and writes e.g. `3 occupied, 29 empty` into the status
  label. If the request fails, the status shows
  `Watch not reachable`.

#### Scenario: Picking an image loads the preview
- **WHEN** the user clicks "Pick image…" and chooses a file
- **THEN** the file dialog returns the chosen path, the image is
  read by Pillow, and the cropped circular preview is rendered to
  the canvas centred at pan=(0,0), zoom=1.

#### Scenario: Dragging the canvas pans the image
- **WHEN** the user presses the left mouse button on the preview
  canvas and drags
- **THEN** the image's pan offset updates by the drag delta and
  the preview re-renders, with the circular mask staying fixed.

#### Scenario: Zoom controls resize the source
- **WHEN** the user scrolls the mouse wheel over the preview
  canvas OR moves the Zoom slider
- **THEN** the zoom factor is clamped to `[0.2, 4.0]` and the
  preview re-renders.

#### Scenario: Upload pushes to the device
- **WHEN** the user clicks "Upload" with a slot N and an image
  loaded
- **THEN** the host composites the image under the circular mask,
  encodes RGB565 little-endian (434 472 bytes), issues
  `POST http://<host>/api/upload?slot=N` with the bytes as the
  request body, waits for `200 OK`, and on success reports
  `Uploaded slot N` in the status line at the bottom of the
  window.

#### Scenario: Upload failure surfaces in the UI
- **WHEN** the device replies with a non-2xx status or the
  connection drops mid-transfer
- **THEN** the status line shows
  `Upload failed: <reason>` and a subsequent "Upload" can retry
  without re-entering the host.

### Requirement: Slot picker, no management
The desktop uploader SHALL expose only a `Slot` Spinbox (integer
input from 0 to 31). It SHALL NOT display the device's current
slot list, allocate new slot numbers, or push/pop from a `+ new`
button. Slot UX on the device is the responsibility of the watch's
`BadgeApp`.

#### Scenario: Slot Spinbox default
- **WHEN** the window opens
- **THEN** the Spinbox shows `0`.

#### Scenario: Spinbox bound to integers
- **WHEN** the user types `4` into the Spinbox
- **THEN** the next Upload sends `POST /api/upload?slot=4`.

## REMOVED Requirements

### Requirement: USB-CDC transport
**Reason**: The serial transport is broken on this hardware in this
environment (the firmware's `Serial` output never reaches COM4 even
with the right FQBN). Wi-Fi is faster (~<1 s vs ~30 s for 434 KB)
and untethers the watch from the USB cable during upload. The user
opted to drop the transport entirely rather than ship a Wi-Fi +
USB-CDC hybrid.

**Migration**: The `badge-uploader` no longer takes `--port` /
`--serial-port`. It takes `--host` (default `192.168.4.1`). The
underlying `protocol.py` and `ports.py` modules are removed;
`wifi.py` replaces them. Existing scripts that called
`badge-uploader push --port COMx` should switch to
`badge-uploader push --host 192.168.4.1`.
