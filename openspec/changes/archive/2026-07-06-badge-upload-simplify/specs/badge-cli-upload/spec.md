## ADDED Requirements

### Requirement: Headless push subcommand
The uploader SHALL provide a `push` subcommand that loads an image,
composites it under the circular mask, encodes RGB565 little-endian,
and sends the bytes over the detected USB-CDC serial port — all
without opening a window.

#### Scenario: Default invocation
- **WHEN** the user runs `badge-uploader push --slot 0 --image ./avatar.png`
- **THEN** the tool reads the image, composites at pan=(0,0),
  zoom=1.0, encodes, sends `BIMG 0 434472\n<bytes>`, waits for `OK`,
  prints `Uploaded slot 0 (434472 bytes)` to stdout, and exits with
  status 0.

#### Scenario: Custom pan and zoom
- **WHEN** the user runs `badge-uploader push --slot 2 --image p.png
  --pan-x -10 --pan-y 5 --zoom 1.2`
- **THEN** the image is composited at pan=(-10, +5), zoom=1.2 before
  encoding, and is sent to slot 2.

#### Scenario: Missing image
- **WHEN** the user runs `badge-uploader push --slot 0 --image nope.png`
  and `nope.png` does not exist
- **THEN** the tool prints `image not found: nope.png` to stderr and
  exits with status 2 (no serial connection is opened).

#### Scenario: Device error
- **WHEN** the device replies `ERR bad_size` after the header is sent
- **THEN** the tool prints `device rejected upload: ERR bad_size` to
  stderr and exits with status 3.

### Requirement: Slot picker, no management
The CLI uploader SHALL take the slot number as a `--slot` argument
(`-s` short form) with default 0. It SHALL NOT query, list, or
auto-allocate slot numbers; that lives on the device.

#### Scenario: Default slot
- **WHEN** the user runs `badge-uploader push --image foo.png`
- **THEN** the slot defaults to 0 and the push targets slot 0.
