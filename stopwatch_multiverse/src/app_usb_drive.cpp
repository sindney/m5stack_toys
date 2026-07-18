// app_usb_drive.cpp — USB Drive app implementation.
//
// Lifecycle:
//
//   onEnter () → count slots → badgefs::unmount() → usbdrive::presentMedia(true)
//   onTick  () → watch for the host's SCSI eject and redraw once
//   onExit  () → usbdrive::presentMedia(false) → badgefs::remount()
//
// Buttons are the shell's business (A+B pops back to the launcher); the
// app only needs to be careful never to touch the FAT mount while the
// host owns the media.

#include "include/app_usb_drive.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <stdio.h>

#include "include/badge_fs.h"
#include "include/usb_drive.h"
#include "include/theme.h"

int UsbDriveApp::countOccupiedSlots() {
    if (!badgefs::mounted()) return 0;
    int n = 0;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/badge/slot_%02d.bin", i);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        if (ftell(f) == (long)IMG_SIZE) ++n;
        fclose(f);
    }
    return n;
}

void UsbDriveApp::onEnter() {
    _slotsUsed  = countOccupiedSlots();
    _wasEjected = false;

    // Hand the partition to the host. The invariant (VFS mounted <=>
    // media absent) is what keeps the host's writes and our FatFS view
    // from ever overlapping.
    if (!badgefs::unmount()) {
        Serial.println("usbdrive app: unmount failed; media NOT presented");
    } else {
        usbdrive::presentMedia(true);
        Serial.println("usbdrive app: media presented");
    }
    draw();
}

void UsbDriveApp::onExit() {
    usbdrive::presentMedia(false);
    badgefs::remount();     // may re-format if the host wiped the volume
    Serial.println("usbdrive app: media hidden, FS remounted");
}

void UsbDriveApp::onTick() {
    bool ej = usbdrive::ejected();
    if (ej != _wasEjected) {
        _wasEjected = ej;
        draw();
    }
}

void UsbDriveApp::onInput(const input::Event& ev) {
    (void)ev;   // nothing app-local; shell handles exit combo
}

void UsbDriveApp::draw() {
    M5GFX& dsp = theme::display();
    dsp.fillScreen(theme::BG);

    const int cx = theme::CX;

    // Title.
    dsp.setTextColor(theme::M_BRIGHT, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(3);
    dsp.drawString("USB DRIVE", cx, 60);

    // Body — drive state first, then what the user does with it.
    int y = 130;
    dsp.setTextDatum(textdatum_t::top_center);
    if (!usbdrive::mediaPresent()) {
        dsp.setTextSize(2);
        dsp.setTextColor(theme::DANGER, theme::BG);
        dsp.drawString("DRIVE OFFLINE", cx, y);
        y += 36;
        dsp.setTextSize(1);
        dsp.setTextColor(theme::TEXT_LO, theme::BG);
        dsp.drawString("unmount failed - see log", cx, y);
    } else if (_wasEjected) {
        dsp.setTextSize(2);
        dsp.setTextColor(theme::ACCENT_2, theme::BG);
        dsp.drawString("EJECTED", cx, y);
        y += 36;
        dsp.setTextSize(2);
        dsp.setTextColor(theme::TEXT_HI, theme::BG);
        dsp.drawString("safe to exit", cx, y);
    } else {
        dsp.setTextSize(2);
        dsp.setTextColor(theme::TEXT_HI, theme::BG);
        dsp.drawString("DRIVE ACTIVE", cx, y);
        y += 36;
        dsp.setTextSize(1);
        dsp.setTextColor(theme::TEXT_LO, theme::BG);
        dsp.drawString("copy SLOT_XX.BIN to BADGE", cx, y);
        y += 24;
        dsp.drawString("then eject in Windows", cx, y);
        y += 36;
        dsp.setTextSize(2);
        dsp.setTextColor(theme::TEXT_LO, theme::BG);
        char line[32];
        snprintf(line, sizeof(line), "slots in use: %d / %d", _slotsUsed, MAX_SLOTS);
        dsp.drawString(line, cx, y);
    }

    // Footer.
    dsp.setTextColor(theme::TEXT_DIM, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(1);
    dsp.drawString("[A]+[B] to exit", cx, 430);

    theme::present();
}
