## ADDED Requirements

### Requirement: Local web server
The uploader SHALL run a local HTTP server bound to `127.0.0.1:5000` by default. Opening the root URL in a browser SHALL display the badge cropping UI.

#### Scenario: Default port
- **WHEN** the user runs `python -m badge_uploader` without flags
- **THEN** the server listens on `127.0.0.1:5000` and prints the URL to stdout

#### Scenario: Custom port
- **WHEN** the user passes `--port 8080`
- **THEN** the server listens on `127.0.0.1:8080` instead

### Requirement: Circular crop UI
The uploader SHALL display the uploaded image on an HTML5 `<canvas>` masked to a 466×466 circle. The user SHALL be able to drag the image to pan and use the mouse wheel to zoom.

#### Scenario: Drag to pan
- **WHEN** the user drags the image inside the canvas
- **THEN** the image translates by the drag delta and the circular mask stays fixed

#### Scenario: Wheel to zoom
- **WHEN** the user scrolls the mouse wheel up over the canvas
- **THEN** the image scales up by a fixed factor (default 1.1) anchored at the cursor position

### Requirement: Circular conversion
On "Send to watch", the uploader SHALL composite the panned and zoomed image under the circular mask with anti-aliased edges, convert the result to RGB565 little-endian, and send it to the device over the serial protocol.

#### Scenario: Edge anti-aliasing
- **WHEN** the converted image is rendered back on the host for preview
- **THEN** the circular edge is smooth (no visible aliasing artifacts within 1 px of the circle)

### Requirement: Serial protocol
The uploader SHALL speak the following protocol on the detected serial port at 115200 baud:
- `LIST` → device replies `SLOTS <n1> <n2> <n3> <n4>` where each `n*` is the slot byte length or 0 if empty.
- `BIMG <slot> <size>\n<bytes>` → device replies `OK` on success or `ERR <reason>`.
- `ERASE <slot>` → device replies `OK` after deleting the slot.

#### Scenario: Successful upload
- **WHEN** the uploader sends `BIMG 0 434472\n<434472 bytes>`
- **THEN** the device writes the bytes to `littlefs:/badge/slot_0.bin` and replies `OK`

#### Scenario: Size mismatch rejected
- **WHEN** the uploader sends `BIMG 0 100\n<100 bytes>`
- **THEN** the device replies `ERR bad_size` and writes nothing

### Requirement: Serial port auto-detection
The uploader SHALL auto-detect the StopWatch's USB-CDC serial port by enumerating `serial.tools.list_ports` and matching on common M5Stack USB VID/PID pairs (0x303A:0x4001, 0x0403:0x6001). The user MAY override with `--port`.

#### Scenario: Auto-detect success
- **WHEN** exactly one matching serial port is present
- **THEN** the uploader connects without prompting

#### Scenario: Multiple candidates
- **WHEN** more than one matching serial port is present
- **THEN** the uploader prints the list and exits with a non-zero status unless `--port` is supplied