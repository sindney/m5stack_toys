## ADDED Requirements

### Requirement: Tkinter upload window
The uploader SHALL provide a native Tkinter window that lets the user
pick an image, position it under a circular mask, preview the result,
and push the encoded RGB565 blob to the watch over the detected
USB-CDC serial port at 115200 baud.

#### Scenario: Window opens on a clean install
- **WHEN** the user runs `badge-uploader ui` (or just `badge-uploader`
  with no subcommand)
- **THEN** a Tkinter window appears with: a "Pick image…" button, a
  466×466 square preview canvas, a circular mask drawn over the
  preview, +/- zoom buttons or a Zoom slider, a Slot Spinbox defaulting
  to 0, and an "Upload" button.

#### Scenario: Picking an image loads the preview
- **WHEN** the user clicks "Pick image…" and chooses a file
- **THEN** the file dialog returns the chosen path, the image is read
  by Pillow, and the cropped circular preview is rendered to the
  canvas centred at pan=(0,0), zoom=1.

#### Scenario: Dragging the canvas pans the image
- **WHEN** the user presses the left mouse button on the preview
  canvas and drags
- **THEN** the image's pan offset updates by the drag delta and the
  preview re-renders, with the circular mask staying fixed.

#### Scenario: Zoom controls resize the source
- **WHEN** the user scrolls the mouse wheel over the preview canvas OR
  moves the Zoom slider
- **THEN** the zoom factor is clamped to `[0.2, 4.0]` and the preview
  re-renders.

#### Scenario: Upload pushes to the device
- **WHEN** the user clicks "Upload" with a slot N and an image loaded
- **THEN** the host composites the image under the circular mask,
  encodes RGB565 little-endian (434 472 bytes), opens the auto-detected
  serial port, sends `BIMG N <size>\n<bytes>`, waits for `OK`, and on
  success reports `Uploaded slot N` in a status line at the bottom of
  the window.

#### Scenario: Upload failure surfaces in the UI
- **WHEN** the device replies `ERR <reason>` or the connection drops
  mid-transfer
- **THEN** the status line shows `Upload failed: <reason>` and the
  serial handle is closed so a retry can re-open it cleanly.

### Requirement: Slot picker, no management
The desktop uploader SHALL expose only a `Slot` Spinbox (integer input
from 0 to 31). It SHALL NOT display the device's current slot list,
allocate new slot numbers, or push/pop from a `+ new` button. Slot UX
on the device is the responsibility of the watch's `BadgeApp`.

#### Scenario: Slot Spinbox default
- **WHEN** the window opens
- **THEN** the Spinbox shows `0`.

#### Scenario: Spinbox bound to integers
- **WHEN** the user types `4` into the Spinbox
- **THEN** the next Upload sends `BIMG 4 434472\n<bytes>`.
