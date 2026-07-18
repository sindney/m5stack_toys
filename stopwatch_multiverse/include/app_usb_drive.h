// app_usb_drive.h — USB Drive app declaration.
//
// Fourth launcher tile. Entering presents the badgeimg partition to the
// host PC as a removable drive (MSC media present); leaving hides the
// media and remounts the on-device read-only FAT view. The host then
// writes SLOT_XX.BIN files directly — by Explorer drag-and-drop or via
// the badge_uploader.html page — no firmware protocol involved.

#pragma once

#include <stdint.h>

#include "app.h"
#include "theme.h"

class UsbDriveApp : public App {
public:
    void onEnter() override;
    void onTick() override;
    void onInput(const input::Event& ev) override;
    void onExit() override;

    const char* name()      const override { return "USB DRIVE"; }
    const char* subtitle() const override { return "badge image transfer"; }
    uint16_t    iconColor() const override { return theme::M_BRIGHT; }

    static constexpr size_t IMG_SIZE  = 466UL * 466UL * 2UL;  // 434 312
    static constexpr int    MAX_SLOTS = 16;

private:
    int  _slotsUsed   = 0;    // counted at enter, before the FS is unmounted
    bool _wasEjected  = false;

    static int countOccupiedSlots();
    void       draw();
};
