## MODIFIED Requirements

### Requirement: Image upload transport
The uploader SHALL upload images from a Python process directly to the
StopWatch over the detected USB-CDC serial port. The transport SHALL
NOT use a local HTTP server, a browser round-trip, or a multi-process
Werkzeug reloader.

#### Scenario: One upload path from Python to device
- **WHEN** the user picks an image and triggers an upload
- **THEN** Python opens the serial port, encodes the image to RGB565
  LE (434 472 bytes), sends `BIMG <slot> <size>\n<bytes>`, and waits
  for `OK` from the device — without any HTTP hop.

### Requirement: Serial protocol
The uploader SHALL speak the following protocol on the detected serial
port at 115200 baud:

- `LIST` → device replies `SLOTS <n1> <n2> ...` where each `n*` is the
  slot byte length, or 0 if empty.
- `BIMG <slot> <size>\n<bytes>` → device replies `OK` on success or
  `ERR <reason>` on failure.
- `ERASE <slot>` → device replies `OK` after deleting the slot.

#### Scenario: Successful upload
- **WHEN** the uploader sends `BIMG 0 434472\n<434472 bytes>`
- **THEN** the device writes the bytes to `littlefs:/badge/slot_0.bin`
  and replies `OK`.

#### Scenario: Size mismatch rejected
- **WHEN** the uploader sends `BIMG 0 100\n<100 bytes>`
- **THEN** the device replies `ERR bad_size` and writes nothing.

### Requirement: Circular conversion
On every upload the uploader SHALL composite the panned and zoomed
image under the 466×466 circular mask with anti-aliased edges, then
convert the result to RGB565 little-endian — exactly 466×466×2 =
434 472 bytes.

#### Scenario: Edge anti-aliasing
- **WHEN** the converted image is previewed back on the host
- **THEN** the circular edge is smooth (no visible aliasing artifacts
  within 1 px of the circle).

### Requirement: Serial port auto-detection
The uploader SHALL auto-detect the StopWatch's USB-CDC serial port by
enumerating `serial.tools.list_ports` and matching on common M5Stack
USB VID/PID pairs (`0x303A:0x4001`, `0x0403:0x6001`). The user MAY
override with `--port` (`-p`).

#### Scenario: Auto-detect success
- **WHEN** exactly one matching serial port is present
- **THEN** the uploader connects without prompting.

#### Scenario: Multiple candidates
- **WHEN** more than one matching serial port is present
- **THEN** the uploader prints the list and exits with a non-zero
  status unless `--port` is supplied.

## REMOVED Requirements

### Requirement: Local web server
**Reason**: The Flask-based HTTP server is the source of the upload
failures. Its Werkzeug reloader forks a child that inherits the
parent's serial handle, and debugging through a browser added three
hop points (browser → Flask → pyserial) where any one dropping the
connection surfaces as a silent failure. A native Python UI talks to
the serial port in a single process and removes the reloader entirely.

**Migration**: Run `badge-uploader` (no arguments) for the Tkinter
window, or `badge-uploader push -s N -i path/to/image.png` for the
CLI. The `--port` / `--serial-port` overrides keep the same meaning
they had under Flask.

### Requirement: HTML crop UI
**Reason**: The HTML canvas + drag/wheel pan/zoom was tightly coupled
to the local web server. With the server gone the same job is done by
a Tkinter `Canvas` widget driven by Pillow's `ImageTk`, so the browser
step is unnecessary.

**Migration**: Same controls (drag to pan, wheel to zoom, +/- to scale)
now live on the native window.

### Requirement: Slot management in the uploader
**Reason**: Slot listing, slot dropdown population, and "+ new"
allocation were convenience UX that the user explicitly asked to defer.
The device's `BadgeApp` already manages the slot list internally and
the auto-advancing slideshow cycles through whatever it finds in
`/badge/`.

**Migration**: The Tkinter window has only a Spinbox for slot number
and the CLI takes `--slot N`. Any future per-slot UX will be built
into the device, not the host.
