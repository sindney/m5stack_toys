## MODIFIED Requirements

The Badge app's storage and slideshow behaviour is unchanged from
the previous version; only the visual layout moves to the flat theme.

### Requirement: Image format and storage

The Badge app SHALL store user-uploaded images as 466×466 RGB565 raw
blobs in LittleFS under the path `littlefs:/badge/slot_N.bin`
where `N` is 0–3. The app SHALL support up to 4 images concurrently.

#### Scenario: Image loads from LittleFS

- **WHEN** the Badge app starts and a slot has a valid image
- **THEN** the image is decoded into a framebuffer and displayed on
  the AMOLED screen

#### Scenario: Missing slot is skipped

- **WHEN** a slot is empty
- **THEN** the slideshow advances past it and never renders a
  blank screen for longer than 200 ms

### Requirement: Slideshow controls

The Badge app SHALL auto-advance every 5 s by default. KEYA SHALL go
to the previous image, KEYB SHALL go to the next image, and a screen
tap SHALL pause or resume auto-advance.

#### Scenario: Manual advance

- **WHEN** the user presses KEYB during the slideshow
- **THEN** the next valid image is shown immediately and the 5 s
  timer resets

#### Scenario: Pause via tap

- **WHEN** the user taps the screen during auto-advance
- **THEN** the auto-advance timer stops and a `PAUSED` indicator
  is shown

### Requirement: Slot management

The Badge app SHALL expose a slot count to the host uploader and
SHALL ignore any upload that targets a slot index outside 0–3.

#### Scenario: Out-of-range slot

- **WHEN** the host sends an upload for slot 5
- **THEN** the firmware replies with `ERR slot_range` and discards
  the payload

### Requirement: Image overwrite safety

The Badge app SHALL refuse to display an image whose byte length
does not equal exactly `466 * 466 * 2 = 434 472` bytes.

#### Scenario: Truncated image rejected

- **WHEN** a slot file is shorter than 434 472 bytes
- **THEN** the slot is treated as empty and the slideshow skips it

### Requirement: Slot indicators and empty state

The Badge app SHALL render the four slot indicators as small dots
near the top of the screen with the current slot lit in
`ACCENT`. When no slots are valid, the Badge app SHALL show
centred text `NO IMAGES` plus a small `Upload via tools/badge_uploader`
hint in `TEXT_LO`.

#### Scenario: Empty state visible

- **WHEN** the Badge app starts with no uploaded images
- **THEN** the screen displays `NO IMAGES` and the helper hint in
  the flat theme colours and never renders a blank screen
