// app_stopwatch.h — Stopwatch app declaration.

#pragma once

#include "app.h"
#include "theme.h"

class StopwatchApp : public App {
public:
    void onEnter() override;
    void onTick() override;
    void onInput(const input::Event& ev) override;
    void onExit() override;

    const char* name()      const override { return "STOPWATCH"; }
    const char* subtitle() const override { return "millisecond timing"; }
    uint16_t    iconColor() const override { return theme::ACCENT; }

private:
    enum class State { Idle, Running, Stopped };

    State       _state        = State::Idle;
    int64_t     _startUs      = 0;     // esp_timer_get_time() at last start
    int64_t     _accUs        = 0;     // accumulated time across stops
    int64_t     _lastLapUs    = 0;     // elapsed at last lap
    int         _lapCount     = 0;
    int64_t     _lastLapDeltaUs = 0;   // most recent lap duration
    char        _lastLapBuf[16] = {0};

    int64_t nowElapsedUs() const;
    void    start();
    void    stop();
    void    lap();
    void    reset();
    void    draw();
};