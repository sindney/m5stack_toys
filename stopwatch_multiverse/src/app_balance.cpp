// app_balance.cpp — Balance (tilt / level) app implementation.

#include "include/app_balance.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "include/ioexpander.h"
#include "include/theme.h"

void BalanceApp::onEnter() {
    _tiltDeg = 0.0f;
    _tiltX   = 0.0f;
    _tiltY   = 0.0f;
    _balanced = true;
    _vibOn    = false;
    _imuReady = false;
    _imuFailCount = 0;
    _sampleCount  = 0;
    _lastSampleMs = millis();

    // Force the BMI270 on before the first sample. M5Unified's
    // BMI270_Class::begin() (run inside M5.begin()) now writes
    // PWR_CTRL=0x06 unconditionally and reads ACC data registers
    // correctly after the patch. This is the safety net.
    M5.Imu.begin();
    _imuReady = M5.Imu.isEnabled();

    draw();
}

void BalanceApp::onExit() {
    if (ioe::ready) {
        ioe::setVibrationDuty(0);
    }
    _vibOn = false;
}

void BalanceApp::onTick() {
    uint32_t now = millis();

    if ((now - _lastSampleMs) >= 20) {
        sample();
        _lastSampleMs = now;
    }

    bool nowBalanced = (_tiltDeg <= BALANCED_THRESH_DEG);
    if (nowBalanced != _balanced) {
        _balanced = nowBalanced;
        if (_balanced && _vibOn) {
            updateVibration();
        }
    }

    if (!_balanced) {
        updateVibration();
    }

    draw();
}

void BalanceApp::sample() {
    if (!M5.Imu.isEnabled()) {
        _lastOk = false;
        _imuReady = false;
        return;
    }
    M5.Imu.update();
    auto data = M5.Imu.getImuData();
    _lastAx = data.accel.x;
    _lastAy = data.accel.y;
    _lastAz = data.accel.z;
    _lastOk = true;
    _sampleCount++;
    float mag = sqrtf(_lastAx * _lastAx + _lastAy * _lastAy + _lastAz * _lastAz);
    _lastMag = mag;
    if (mag > 0.01f) {
        _tiltDeg = acosf(_lastAz / mag) * (180.0f / 3.14159265f);
        _tiltX   = _lastAx / mag;
        _tiltY   = _lastAy / mag;
    }
    _imuReady = true;
}

void BalanceApp::updateVibration() {
    if (!ioe::ready) return;

    if (_balanced) {
        if (_vibOn) {
            ioe::setVibrationDuty(0);
            _vibOn = false;
        }
        return;
    }

    uint32_t now = millis();
    constexpr uint32_t period = 500;
    bool wantOn = ((now / (period / 2)) % 2) == 0;
    if (wantOn != _vibOn) {
        ioe::setVibrationDuty(wantOn ? 128 : 0);
        _vibOn = wantOn;
        _lastVibToggleMs = now;
    }
}

void BalanceApp::draw() {
    M5GFX& dsp = theme::display();
    dsp.fillScreen(theme::BG);

    // Title + state.
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextColor(theme::M_BRIGHT, theme::BG);
    dsp.setTextSize(3);
    dsp.drawString("BALANCE", theme::CX, 100);

    dsp.setTextColor(_balanced ? theme::M_BRIGHT : theme::ACCENT_2,
                     theme::BG);
    dsp.setTextSize(2);
    dsp.drawString(_balanced ? "BALANCED" : "UNBALANCED",
                   theme::CX, 150);

    // Bracketed dial disc.
    const int dialR = 95;
    const int cx    = theme::CX;
    const int cy    = 270;

    dsp.fillCircle(cx, cy, dialR, theme::SURFACE);
    dsp.drawCircle(cx, cy, dialR,
                   _balanced ? theme::M_BRIGHT : theme::ACCENT_2);
    theme::drawCorners(cx - dialR, cy - dialR, dialR * 2, dialR * 2,
                       _balanced ? theme::M_BRIGHT : theme::ACCENT_2, 16);

    // Centre crosshair.
    dsp.drawLine(cx - dialR / 2, cy, cx + dialR / 2, cy, theme::TEXT_DIM);
    dsp.drawLine(cx, cy - dialR / 2, cx, cy + dialR / 2, theme::TEXT_DIM);

    // Bubble.
    float scale = static_cast<float>(dialR - 18);
    int bx = cx + static_cast<int>(_tiltX * scale * 2.0f);
    int by = cy + static_cast<int>(_tiltY * scale * 2.0f);
    int dx = bx - cx, dy = by - cy;
    float dist = sqrtf(static_cast<float>(dx * dx + dy * dy));
    if (dist > scale) {
        bx = cx + static_cast<int>(dx * scale / dist);
        by = cy + static_cast<int>(dy * scale / dist);
    }
    dsp.fillCircle(bx, by, 14, _balanced ? theme::M_BRIGHT : theme::DANGER);
    dsp.drawCircle(bx, by, 14, theme::BG);

    // Direction stub from bubble back to centre.
    if (abs(dx) + abs(dy) > 6) {
        int ax2 = bx + (cx - bx) / 2;
        int ay2 = by + (cy - by) / 2;
        dsp.drawLine(bx, by, ax2, ay2, theme::TEXT_LO);
    }

    // Single status line: degrees + state, centred below the dial.
    char status[32];
    snprintf(status, sizeof(status), "%.1f deg",
             _tiltDeg);
    dsp.setTextColor(_balanced ? theme::TEXT_HI : theme::ACCENT_2, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(2);
    dsp.drawString(status, cx, cy + dialR + 22);

    // 45° edge hints rendered INTO the backbuffer at the same anchors
    // as the launcher / every other screen.
    bool imuAlive = M5.Imu.isEnabled();
    theme::drawEdgeHint(95, 95, -45.0f,
                        "[A]", imuAlive ? "live" : "no imu",
                        imuAlive ? theme::M_BRIGHT : theme::DANGER,
                        theme::TEXT_LO);
    theme::drawEdgeHint(371, 95, 45.0f,
                        "[B]", "exit",
                        theme::M_BRIGHT, theme::TEXT_LO);

    // Push the composed frame to the panel as one shot.
    theme::present();
}

void BalanceApp::onInput(const input::Event& /*ev*/) {}
