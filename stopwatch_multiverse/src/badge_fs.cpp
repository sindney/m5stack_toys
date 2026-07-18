// badge_fs.cpp — see badge_fs.h for the module contract.
//
// Format path: mount_ro fails on blank flash → we register our own
// WRITABLE raw diskio driver (IDF's diskio_rawflash is read-only by
// design — ff_raw_write() returns RES_WRPRT, so f_mkfs dies with
// FR_WRITE_PROTECTED) backed by usbdrive::rawRead/rawWrite, f_mkfs a
// FAT16 SFD volume, label it, unregister, and mount_ro as usual.

#include "include/badge_fs.h"

#include <Arduino.h>
#include <esp_partition.h>
#include <esp_vfs_fat.h>
#include <diskio_impl.h>
#include <ff.h>

#include "include/usb_drive.h"

namespace {

constexpr const char* MOUNT_PATH = "/badge";
constexpr const char* PART_LABEL = "badgeimg";

bool s_mounted = false;

bool mountRo() {
    esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed   = false,
        .max_files                = 4,
        .allocation_unit_size     = 0,
        .disk_status_check_enable = false,
        .use_one_fat              = false,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_ro(MOUNT_PATH, PART_LABEL, &cfg);
    if (err == ESP_OK) {
        s_mounted = true;
        return true;
    }
    Serial.printf("badgefs: mount_ro failed: %s\n", esp_err_to_name(err));
    return false;
}

// ---- writable raw diskio driver (format-only) ---------------------------

constexpr uint32_t FS_SECTOR = 512;

DSTATUS fmtInit(BYTE)   { return 0; }
DSTATUS fmtStatus(BYTE) { return 0; }

DRESULT fmtRead(BYTE, BYTE* buff, DWORD sector, UINT count) {
    return usbdrive::rawRead(sector * FS_SECTOR, buff, count * FS_SECTOR)
               ? RES_OK : RES_ERROR;
}

DRESULT fmtWrite(BYTE, const BYTE* buff, DWORD sector, UINT count) {
    return usbdrive::rawWrite(sector * FS_SECTOR, buff, count * FS_SECTOR)
               ? RES_OK : RES_ERROR;
}

DRESULT fmtIoctl(BYTE, BYTE cmd, void* buff) {
    switch (cmd) {
        case CTRL_SYNC:       return RES_OK;
        case GET_SECTOR_SIZE: *(WORD*)buff  = FS_SECTOR;                    return RES_OK;
        case GET_BLOCK_SIZE:  *(DWORD*)buff = 4096 / FS_SECTOR;             return RES_OK;
        case GET_SECTOR_COUNT: {
            const esp_partition_t* p = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
                PART_LABEL);
            if (!p) return RES_ERROR;
            *(DWORD*)buff = p->size / FS_SECTOR;
            return RES_OK;
        }
    }
    return RES_PARERR;
}

const ff_diskio_impl_t FMT_IMPL = {
    .init  = fmtInit,
    .status = fmtStatus,
    .read  = fmtRead,
    .write = fmtWrite,
    .ioctl = fmtIoctl,
};

// One-time recovery: create a fresh FAT16 volume labelled BADGE.
bool formatPartition() {
    constexpr BYTE DRV = 0;
    ff_diskio_register(DRV, &FMT_IMPL);

    Serial.println("badgefs: formatting badgeimg (one-time)...");
    uint32_t t0 = millis();
    MKFS_PARM opt = {
        .fmt    = FM_FAT | FM_SFD,  // FAT16, no MBR — LBA0 is the boot sector
        .n_fat  = 1,
        .align  = 0,
        .n_root = 0,
        .au_size = 0,               // let f_mkfs pick cluster size
    };
    static uint8_t work[4096];
    FRESULT fr = f_mkfs("0:", &opt, work, sizeof(work));
    Serial.printf("badgefs: f_mkfs -> %d in %lu ms\n", fr,
                  (unsigned long)(millis() - t0));
    if (fr == FR_OK) {
        f_setlabel("0:BADGE");      // cosmetic; ignore the result
    }
    ff_diskio_unregister(DRV);
    return fr == FR_OK;
}

}  // namespace

namespace badgefs {

bool begin() {
    if (s_mounted) return true;
    if (mountRo()) return true;
    if (!formatPartition()) return false;
    return mountRo();
}

bool mounted() { return s_mounted; }

bool unmount() {
    if (!s_mounted) return true;
    esp_err_t err = esp_vfs_fat_spiflash_unmount_ro(MOUNT_PATH, PART_LABEL);
    if (err != ESP_OK) {
        Serial.printf("badgefs: unmount failed: %s\n", esp_err_to_name(err));
        return false;
    }
    s_mounted = false;
    return true;
}

bool remount() {
    if (s_mounted) return true;
    return begin();
}

}  // namespace badgefs
