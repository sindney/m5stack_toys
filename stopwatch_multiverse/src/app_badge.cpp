// app_badge.cpp — Badge slideshow app implementation.
//
// Reads `/badge/slot_XX.bin` files from the read-only FAT mount of the
// badgeimg partition (see badge_fs.cpp). The host PC writes those files
// while the USB Drive app presents the volume; this app re-scans the
// slot map every time it is entered.

#include "include/app_badge.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <stdio.h>

#include "include/shell.h"
#include "include/theme.h"

BadgeApp::BadgeApp() {
    _fb = static_cast<uint16_t*>(ps_malloc(IMG_SIZE));
    if (!_fb) {
        _fb = static_cast<uint16_t*>(malloc(IMG_SIZE));
    }
}

void BadgeApp::onEnter() {
    _mode = Mode::Loading;
    refreshSlotMap();

    if (_slotCount > 0 && loadSlot(_slotNumber[0])) {
        _currentSlot    = _slotNumber[0];
        _currentSlotIdx = 0;
        _mode           = Mode::Browsing;
        _lastAdvanceMs  = millis();
    }
    draw();
}

void BadgeApp::onExit() {}

void BadgeApp::onTick() {
    if (_mode != Mode::Browsing) return;

    if ((millis() - _lastAdvanceMs) >= 5000) {
        int nxt = nextValidSlot(_currentSlotIdx, +1);
        if (nxt != _currentSlotIdx && loadSlot(_slotNumber[nxt])) {
            _currentSlot    = _slotNumber[nxt];
            _currentSlotIdx = nxt;
            _lastAdvanceMs  = millis();
            draw();
        }
    }
}

void BadgeApp::onInput(const input::Event& ev) {
    switch (ev.kind) {
        case input::EventKind::Click:
            if (ev.button == input::Button::KEYA) {
                int prv = nextValidSlot(_currentSlotIdx, -1);
                if (prv != _currentSlotIdx && loadSlot(_slotNumber[prv])) {
                    _currentSlot    = _slotNumber[prv];
                    _currentSlotIdx = prv;
                    _lastAdvanceMs  = millis();
                    draw();
                }
            } else if (ev.button == input::Button::KEYB) {
                int nxt = nextValidSlot(_currentSlotIdx, +1);
                if (nxt != _currentSlotIdx && loadSlot(_slotNumber[nxt])) {
                    _currentSlot    = _slotNumber[nxt];
                    _currentSlotIdx = nxt;
                    _lastAdvanceMs  = millis();
                    draw();
                }
            }
            break;

        case input::EventKind::TouchDown:
            if (_mode == Mode::Browsing) {
                _mode = Mode::Paused;
                draw();
            } else if (_mode == Mode::Paused) {
                _mode          = Mode::Browsing;
                _lastAdvanceMs = millis();
                draw();
            }
            break;

        default: break;
    }
}

const char* BadgeApp::slotPath(int slot) {
    static char path[32];
    snprintf(path, sizeof(path), "/badge/slot_%02d.bin", slot);
    return path;
}

void BadgeApp::refreshSlotMap() {
    _slotCount = 0;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        _slotValid[i] = false;
        _slotNumber[i] = 0;
    }

    for (int i = 0; i < MAX_SLOTS; ++i) {
        FILE* f = fopen(slotPath(i), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        if (ftell(f) == (long)IMG_SIZE) {
            _slotValid[_slotCount] = true;
            _slotNumber[_slotCount] = i;
            _slotCount++;
        }
        fclose(f);
    }
}

bool BadgeApp::loadSlot(int slot) {
    if (slot < 0) return false;
    FILE* f = fopen(slotPath(slot), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    if (ftell(f) != (long)IMG_SIZE) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);

    size_t got = fread(_fb, 1, IMG_SIZE, f);
    fclose(f);
    if (got != IMG_SIZE) return false;

    _currentSlot = slot;
    return true;
}

int BadgeApp::findSlotIndex(int slot) const {
    for (int i = 0; i < _slotCount; ++i) {
        if (_slotNumber[i] == slot) return i;
    }
    return -1;
}

int BadgeApp::nextValidSlot(int from, int dir) const {
    if (_slotCount == 0) return -1;
    if (from < 0) from = 0;
    int n = _slotCount;
    for (int step = 1; step <= n; ++step) {
        int idx = ((from + dir * step) % n + n) % n;
        if (_slotValid[idx]) return idx;
    }
    return from;
}

void BadgeApp::draw() {
    M5GFX& dsp = theme::display();

    if (_currentSlot < 0) {
        // Empty state — no chrome worth drawing without an image.
        dsp.fillScreen(theme::BG);
        dsp.setTextColor(theme::TEXT_HI, theme::BG);
        dsp.setTextDatum(textdatum_t::top_center);
        dsp.setTextSize(3);
        dsp.drawString("NO IMAGES", theme::CX, 200);
        dsp.setTextColor(theme::TEXT_LO, theme::BG);
        dsp.setTextSize(1);
        dsp.drawString("copy images via USB DRIVE app", theme::CX, 250);
    } else {
        // Fullscreen: _fb holds RGB565 LITTLE-ENDIAN bytes read straight
        // from the slot file — already the full 466×466 frame, pre-masked
        // to the round face by the host tool. pushImage(uint16_t*) would
        // treat them as swap565_t (big-endian, raw blit — see
        // LGFXBase::create_pc(const uint16_t*)), scrambling every colour.
        // Cast to rgb565_t* so the converting path runs: host-order
        // value -> panel order.
        dsp.pushImage(0, 0, 466, 466,
                      reinterpret_cast<const lgfx::rgb565_t*>(_fb));

        if (_mode == Mode::Paused) {
            // Transient state chip — the only overlay a paused slideshow
            // gets, so tap-to-pause has visible feedback.
            dsp.setTextColor(theme::BG, theme::M_BRIGHT);
            dsp.setTextDatum(textdatum_t::top_center);
            dsp.setTextSize(2);
            dsp.drawString("[PAUSED]", theme::CX, 40);
        }
    }

    theme::present();
}
