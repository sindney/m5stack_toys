// badge_fs.h — the device-side view of the `badgeimg` raw FAT partition.
//
// The host PC owns the partition while the USB Drive app presents it over
// MSC. The device only ever mounts it READ-ONLY (esp_vfs_fat_spiflash_
// mount_ro — no wear levelling), which guarantees the layout FatFS parses
// is byte-identical to the LBAs the host wrote. The single invariant:
//
//     VFS mounted here  <=>  MSC media absent on the host
//
// is kept by UsbDriveApp (unmount on enter, remount on exit).
//
// On first boot (blank or corrupt partition) begin() formats the volume
// as FAT16 "super-floppy" (no MBR — LBA0 is the boot sector, which is
// exactly what the MSC layer exposes) and labels it BADGE.

#pragma once

namespace badgefs {

// Mount /badge read-only, formatting the partition on first use.
// Returns false when neither mount nor format succeeded — callers keep
// working, the Badge app simply shows NO IMAGES.
bool begin();

bool mounted();

// Drop the VFS mount (before the host gets the media) and re-acquire it
// (after the host is done). remount() re-tries begin()-style recovery.
bool unmount();
bool remount();

}  // namespace badgefs
