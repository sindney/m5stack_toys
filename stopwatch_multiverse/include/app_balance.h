// app_balance.h — Balance (tilt / level) app declaration.

#pragma once

#include "app.h"
#include "theme.h"

class BalanceApp : public App {
public:
    void onEnter() override;
    void onTick() override;
    void onInput(const input::Event& ev) override;
    void onExit() override;

    const char* name()      const override { return "BALANCE"; }
    const char* subtitle() const override { return "tilt + haptic"; }
    uint16_t    iconColor() const override { return theme::ACCENT; }

private:
    static constexpr float BALANCED_THRESH_DEG = 2.0f;

    float       _tiltDeg = 0.0f;       // angle between +Z and gravity
    float       _tiltX   = 0.0f;       // for the bubble X offset
    float       _tiltY   = 0.0f;       // for the bubble Y offset
    bool        _balanced = true;
    bool        _vibOn    = false;
    bool        _imuReady  = false;    // flips true on first successful read
    bool        _lastOk    = false;    // result of the most recent getAccel
    float       _lastAx    = 0.0f;
    float       _lastAy    = 0.0f;
    float       _lastAz    = 0.0f;
    float       _lastMag   = 0.0f;
    uint32_t    _lastVibToggleMs = 0;
    uint32_t    _lastSampleMs = 0;
    uint32_t    _sampleCount = 0;       // for debug overlay
    uint32_t    _imuFailCount = 0;      // for debug overlay

    void sample();
    void updateVibration();
    void draw();
};