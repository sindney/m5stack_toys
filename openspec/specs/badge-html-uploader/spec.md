# badge-html-uploader Specification

## Purpose
TBD - created by archiving change badge-usb-drive. Update Purpose after archive.
## Requirements
### Requirement: Single-file, zero-install host tool
The host tool SHALL be one self-contained HTML file
(`tools/badge_uploader.html`) requiring no installation, build step,
or server. It SHALL work opened directly from disk in Chrome/Edge.

#### Scenario: Open and use
- **WHEN** the user opens the file in Chrome or Edge
- **THEN** the full UI (image drop, preview, slot grid, connect
  button) is available with no network access.

### Requirement: On-page compositing to the watch's format
The tool SHALL composite a user image onto a 466×466 canvas — fit-then
-zoom, centre+pan, circular mask of radius 233 with a 1.5 px feathered
edge, alpha premultiplied against black — and SHALL encode it as
exactly 434 312 bytes of RGB565 little-endian. Writes SHALL validate
the byte count.

#### Scenario: Drag, zoom, encode
- **WHEN** the user drops a photo and adjusts pan/zoom
- **THEN** the preview shows the circular result and the encoder
  produces exactly 434 312 bytes with RGB565 LE byte order
  (red → `0x00 0xF8`).

### Requirement: Direct-to-drive writes via File System Access API
The tool SHALL offer a "Connect badge drive" action using
`showDirectoryPicker({mode:'readwrite'})`, SHALL list 16 slots with
occupied state derived from `slot_XX.bin` files of exactly
434 312 bytes, SHALL write the selected slot via `createWritable`,
and SHALL erase via `removeEntry`. If the picked directory contains a
`badge` subfolder the tool SHALL use it.

#### Scenario: Write slot end-to-end
- **WHEN** the user connects the BADGE drive, picks slot 3, and clicks
  write
- **THEN** `slot_03.bin` (434 312 bytes) exists on the drive and the
  watch's Badge app displays it after eject + app re-entry.

#### Scenario: Erase
- **WHEN** the user selects an occupied slot and clicks erase
- **THEN** the file is removed and the slot grid shows it empty.

### Requirement: Fallback without File System Access API
On browsers without `showDirectoryPicker`, the tool SHALL disable the
drive features and offer a `.bin` download for manual drag-and-drop
onto the drive.

#### Scenario: Fallback download
- **WHEN** the page runs in a browser without the API
- **THEN** the status line explains the fallback and "Download .bin"
  produces a correctly named `slot_XX.bin` file.

