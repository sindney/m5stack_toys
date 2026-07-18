// usb_drive.cpp — see usb_drive.h for the module contract.
//
// The MSC callbacks serve the raw `badgeimg` partition. NOR flash can
// only program 1→0, so onWrite is read-modify-erase-write per 4 KB
// flash sector: without the read-back step, a 512-byte host write would
// wipe the other seven 512-byte blocks that share its flash sector.

#include "include/usb_drive.h"

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_partition.h>

namespace {

// Static instance: the USBMSC constructor registers the MSC interface
// with the TinyUSB stack BEFORE tinyusb_init() runs (that only happens
// inside USB.begin(), called from usbdrive::begin()). The global Serial
// object did the same for CDC, so one USB.begin() brings up the
// composite device in a single enumeration.
USBMSC g_msc;

const esp_partition_t* s_part     = nullptr;
bool                   s_present  = false;
bool                   s_ejected  = false;

constexpr uint32_t MSC_BLOCK    = 512;     // what the host addresses
constexpr uint32_t FLASH_SECTOR = 4096;    // erase granularity
uint8_t s_scratch[FLASH_SECTOR];

const esp_partition_t* getPartition() {
    if (!s_part) {
        s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                          ESP_PARTITION_SUBTYPE_DATA_FAT,
                                          "badgeimg");
    }
    return s_part;
}

int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    uint32_t addr = lba * MSC_BLOCK + offset;
    return usbdrive::rawRead(addr, buffer, bufsize) ? (int32_t)bufsize : -1;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    uint32_t addr = lba * MSC_BLOCK + offset;
    return usbdrive::rawWrite(addr, buffer, bufsize) ? (int32_t)bufsize : -1;
}

bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    Serial.printf("usbdrive: START/STOP start=%u eject=%u\n", start, load_eject);
    if (load_eject && !start) {
        s_ejected = true;
    } else if (start) {
        s_ejected = false;
    }
    return true;
}

}  // namespace

namespace usbdrive {

bool rawRead(uint32_t addr, void* data, uint32_t len) {
    const esp_partition_t* p = getPartition();
    if (!p || addr + len > p->size) return false;
    return esp_partition_read(p, addr, data, len) == ESP_OK;
}

bool rawWrite(uint32_t addr, const void* data, uint32_t len) {
    const esp_partition_t* p = getPartition();
    if (!p || addr + len > p->size) return false;

    const uint8_t* src = static_cast<const uint8_t*>(data);
    while (len > 0) {
        uint32_t sector    = addr & ~(FLASH_SECTOR - 1);
        uint32_t in_sector = addr - sector;
        uint32_t chunk     = FLASH_SECTOR - in_sector;
        if (chunk > len) chunk = len;

        if (esp_partition_read(p, sector, s_scratch, FLASH_SECTOR) != ESP_OK) {
            return false;
        }
        memcpy(s_scratch + in_sector, src, chunk);
        if (esp_partition_erase_range(p, sector, FLASH_SECTOR) != ESP_OK) {
            return false;
        }
        if (esp_partition_write(p, sector, s_scratch, FLASH_SECTOR) != ESP_OK) {
            return false;
        }

        addr += chunk;
        src  += chunk;
        len  -= chunk;
    }
    return true;
}

bool begin() {
    if (!getPartition()) {
        Serial.println("usbdrive: badgeimg partition MISSING");
        return false;
    }

    g_msc.vendorID("M5Stack");      // max 8 chars
    g_msc.productID("StopWatch");   // max 16 chars
    g_msc.productRevision("1.0");   // max 4 chars
    g_msc.onRead(onRead);
    g_msc.onWrite(onWrite);
    g_msc.onStartStop(onStartStop);
    g_msc.mediaPresent(false);      // empty card reader until the app asks
    g_msc.isWritable(true);
    if (!g_msc.begin(s_part->size / MSC_BLOCK, MSC_BLOCK)) {
        Serial.println("usbdrive: MSC begin failed");
        return false;
    }

    // First and only tinyusb_init(): CDC (global Serial) + MSC (g_msc)
    // come up as one composite device.
    USB.begin();

    Serial.printf("usbdrive: badgeimg @0x%06lx, %lu KB, %lu MSC blocks\n",
                  (unsigned long)s_part->address,
                  (unsigned long)(s_part->size / 1024),
                  (unsigned long)(s_part->size / MSC_BLOCK));
    return true;
}

void presentMedia(bool present) {
    s_present = present;
    if (present) s_ejected = false;
    g_msc.mediaPresent(present);
}

bool mediaPresent() { return s_present; }

bool ejected() { return s_ejected; }
void clearEjected() { s_ejected = false; }

}  // namespace usbdrive
