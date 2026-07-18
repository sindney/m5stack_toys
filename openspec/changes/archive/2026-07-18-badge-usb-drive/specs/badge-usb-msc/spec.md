## ADDED Requirements

### Requirement: USB composite device on the watch
The firmware SHALL enumerate as a TinyUSB composite device — a CDC
serial console plus an MSC card reader — as part of `setup()`, before
the launcher starts. The MSC card reader SHALL be backed by the raw
`badgeimg` flash partition and SHALL report no media until the USB
Drive app presents it.

#### Scenario: Composite enumerates on a desktop host
- **WHEN** the watch boots connected to a PC by USB
- **THEN** a serial COM port (CDC console carrying boot logs) and a
  USB mass-storage card reader both appear, and no drive letter is
  mounted (media absent).

#### Scenario: Debug console carries boot logs
- **WHEN** a host opens the CDC port at 115 200 baud
- **THEN** firmware log lines (boot banner, badgefs/usbdrive messages)
  are received — no output requires a running app.

### Requirement: badgeimg partition layout
The partition table SHALL place `app0` (ota_0) at 0x10000 and provide a
`badgeimg` data partition (subtype `fat`) at 0x610000 of size
0x9E0000. Badge images SHALL NOT share a partition with any other
data, and SHALL survive firmware reflashing and reboots.

#### Scenario: Reflash keeps images
- **WHEN** new firmware is flashed over USB
- **THEN** every `slot_XX.bin` previously written to the BADGE drive
  is still present and byte-identical.

### Requirement: Host writes use read-modify-erase-write
MSC write callbacks SHALL implement read-modify-erase-write per 4 KB
flash sector: read the sector, merge the incoming bytes, erase the
sector, program it back. A 512-byte host write SHALL NOT corrupt
neighbouring blocks.

#### Scenario: Byte-identical round trip
- **WHEN** the host copies a 434 312-byte file onto the drive
- **THEN** the device reads the same file back through its FAT mount
  with identical length and content (checksum match).

### Requirement: Device mounts badgeimg read-only
The device SHALL mount `/badge` via `esp_vfs_fat_spiflash_mount_ro`
(no wear levelling). While the MSC media is presented to a host, the
device SHALL NOT have the volume mounted, and vice versa.

#### Scenario: Invariant on app transitions
- **WHEN** the user enters the USB Drive app
- **THEN** the firmware unmounts `/badge` before presenting the media,
  and remounts it only after the media is hidden on exit.

### Requirement: First-boot self-format
If the read-only mount fails, the firmware SHALL format the partition
as FAT16 (super-floppy layout, no MBR) labelled `BADGE` using a
writable diskio driver, then mount it. The format SHALL NOT use IDF's
`diskio_rawflash` (read-only by design).

#### Scenario: Blank flash formats itself
- **WHEN** the watch boots with an erased or corrupted badgeimg
  partition
- **THEN** a FAT16 volume named BADGE is created automatically and the
  next media-present shows a formatted, empty drive.

### Requirement: USB Drive app on the launcher
The firmware SHALL expose a fourth launcher tile "USB Drive". Entering
it SHALL present the media (drive letter appears on the host within
~2 s); the AMOLED SHALL show the drive state, the slot usage count,
and an eject hint. Holding KEYA+KEYB SHALL hide the media and return
to the launcher. A host-side eject SHALL be reflected on the AMOLED.

#### Scenario: Drive appears and disappears with the app
- **WHEN** the user enters the USB Drive app and later exits
- **THEN** the BADGE drive appears on the PC on entry and disappears
  on exit, and a Windows eject while inside the app shows an
  "ejected — safe to exit" state.

### Requirement: Badge app reads slots from the FAT mount
The Badge app SHALL scan `/badge/slot_00.bin` … `slot_15.bin`
(16 slots) on entry, treat exactly-434 312-byte files as valid RGB565
LE frames, and cycle them in the slideshow. It SHALL NOT reference
LittleFS or any upload protocol.

#### Scenario: Host-written image displays
- **WHEN** the host has written a valid `slot_XX.bin` and the user
  opens the Badge app
- **THEN** the image is shown and participates in the 5 s auto-advance
  cycle.
