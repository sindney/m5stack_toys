// app_badge.h — Badge slideshow app declaration.
//
// Reads `/badge/slot_XX.bin` files (zero-padded, 8.3-safe names) from the
// read-only FAT view of the badgeimg partition. Files land there from the
// host PC while the USB Drive app presents the volume over MSC; re-entering
// this app re-scans the slot map.

#pragma once

#include <stdint.h>

#include "app.h"
#include "theme.h"

class BadgeApp : public App {
public:
    BadgeApp();

    void onEnter() override;
    void onTick() override;
    void onInput(const input::Event& ev) override;
    void onExit() override;

    const char* name()      const override { return "BADGE"; }
    const char* subtitle() const override { return "image slideshow"; }
    uint16_t    iconColor() const override { return theme::M_BRIGHT; }

private:
    static constexpr int IMG_PIXELS = 466 * 466;
    static constexpr size_t IMG_SIZE = IMG_PIXELS * 2;
    // 16 slots x 434 KB = 6.9 MB — comfortably inside the ~9.9 MB
    // badgeimg FAT partition, with headroom for the host's filesystem
    // bookkeeping. (The old 32-slot cap was a lie: it never fit.)
    static constexpr int MAX_SLOTS = 16;

    enum class Mode { Loading, Browsing, Paused };

    Mode        _mode           = Mode::Loading;
    int         _currentSlot    = -1;        // slot number (file name) actually shown
    uint32_t    _lastAdvanceMs  = 0;
    bool        _slotValid[MAX_SLOTS] = {false};
    int         _slotNumber[MAX_SLOTS] = {0};  // file numbers
    int         _slotCount      = 0;
    int         _currentSlotIdx = -1;        // index into _slotNumber[]
    uint16_t*   _fb             = nullptr;   // 466×466 RGB565 framebuffer

    bool        loadSlot(int slot);
    int         nextValidSlot(int from, int dir) const;
    int         findSlotIndex(int slot) const;
    static const char* slotPath(int slot);
    void        refreshSlotMap();
    void        draw();
};
