// usb_drive.h — TinyUSB composite device: CDC debug console + MSC card
// reader exposing the raw `badgeimg` flash partition to the host PC.
//
// The watch enumerates as a composite USB device as soon as setup() runs
// (USB.begin() is never auto-called before setup on this platform — no
// ARDUINO_USB_ON_BOOT flag is set — so the global Serial (CDC) and the
// static USBMSC instance here both register their interfaces before the
// single tinyusb_init()).
//
// The MSC media starts ABSENT: the host sees an empty card reader. The
// USB Drive app calls presentMedia(true) after unmounting the on-device
// FAT view, and presentMedia(false) before remounting it. While media is
// present the host owns the partition — firmware must not touch the FS.

#pragma once

#include <stdint.h>

namespace usbdrive {

// Find the `badgeimg` partition, register MSC callbacks, and start the
// USB stack. Returns false (and logs) when the partition is missing —
// the rest of the firmware keeps working, just without USB storage.
bool begin();

// Show/hide the storage media on the host. present(true) makes the drive
// letter appear; present(false) makes it vanish (like removing a card).
void presentMedia(bool present);
bool mediaPresent();

// Latched when the host "ejects" the media (SCSI START/STOP with
// load_eject). The USB Drive app shows "safe to unplug" from this.
bool ejected();
void clearEjected();

// Block-level access to the badgeimg partition (byte addresses), shared
// with the first-boot formatter in badge_fs. rawWrite is
// read-modify-erase-write per 4 KB flash sector; rawRead is a straight
// esp_partition_read. Both locate the partition lazily, so they work
// before usbdrive::begin() has run.
bool rawRead(uint32_t addr, void* data, uint32_t len);
bool rawWrite(uint32_t addr, const void* data, uint32_t len);

}  // namespace usbdrive
