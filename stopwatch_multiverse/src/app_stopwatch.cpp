// app_stopwatch.cpp — Stopwatch app implementation.

#include "include/app_stopwatch.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <M5Unified.h>

#include "include/theme.h"

namespace {

void drawTitleAndState(const char* state, uint16_t stateColor) {
    M5GFX& dsp = theme::display();
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextColor(stateColor, theme::BG);
    dsp.setTextSize(3);
    dsp.drawString("STOPWATCH", theme::CX, 110);

    dsp.setTextColor(stateColor, theme::BG);
    dsp.setTextSize(2);
    dsp.drawString(state, theme::CX, 160);
}

}  // namespace

void StopwatchApp::onEnter() {
    _state      = State::Idle;
    _accUs      = 0;
    _lapCount   = 0;
    _lastLapUs  = 0;
    _lastLapDeltaUs = 0;
    _lastLapBuf[0] = 0;
    draw();
}

void StopwatchApp::onExit() {}

void StopwatchApp::onTick() {
    if (_state == State::Running) draw();
}

void StopwatchApp::onInput(const input::Event& ev) {
    switch (ev.kind) {
        case input::EventKind::Click:
            if (ev.button == input::Button::KEYA) {
                if (_state == State::Running) stop();
                else                           start();
            } else if (ev.button == input::Button::KEYB) {
                if (_state == State::Running) lap();
                else if (_state == State::Stopped) reset();
            }
            draw();
            break;

        case input::EventKind::TouchDown:
            if (_state == State::Running) stop();
            else                          start();
            draw();
            break;

        default: break;
    }
}

int64_t StopwatchApp::nowElapsedUs() const {
    int64_t total = _accUs;
    if (_state == State::Running) {
        total += (esp_timer_get_time() - _startUs);
    }
    return total;
}

void StopwatchApp::start() {
    if (_state == State::Running) return;
    _startUs = esp_timer_get_time();
    _state   = State::Running;
}

void StopwatchApp::stop() {
    if (_state != State::Running) return;
    _accUs += (esp_timer_get_time() - _startUs);
    _state = State::Stopped;
}

void StopwatchApp::lap() {
    if (_state != State::Running) return;
    int64_t elapsed = nowElapsedUs();
    _lastLapDeltaUs = elapsed - _lastLapUs;
    _lastLapUs      = elapsed;
    _lapCount++;
    int mins = static_cast<int>((_lastLapDeltaUs / 60000000));
    int secs = static_cast<int>((_lastLapDeltaUs / 1000000) % 60);
    int ms   = static_cast<int>((_lastLapDeltaUs % 1000000) / 1000);
    snprintf(_lastLapBuf, sizeof(_lastLapBuf),
             "L%d %d:%02d.%03d", _lapCount, mins, secs, ms);
}

void StopwatchApp::reset() {
    _state          = State::Idle;
    _accUs          = 0;
    _lapCount       = 0;
    _lastLapUs      = 0;
    _lastLapDeltaUs = 0;
    _lastLapBuf[0]  = 0;
}

static void formatElapsed(int64_t us, char* out, size_t n) {
    int64_t ms = (us + 500) / 1000;
    int hours   = static_cast<int>((ms / 3600000) % 100);
    int minutes = static_cast<int>((ms / 60000) % 60);
    int seconds = static_cast<int>((ms / 1000) % 60);
    int millis  = static_cast<int>(ms % 1000);
    snprintf(out, n, "%02d:%02d:%02d.%03d", hours, minutes, seconds, millis);
}

void StopwatchApp::draw() {
    M5GFX& dsp = theme::display();
    dsp.fillScreen(theme::BG);

    // Title + state.
    const char* stateStr =
        (_state == State::Idle)    ? "READY"   :
        (_state == State::Running) ? "RUNNING" : "PAUSED";
    const uint16_t stateColor =
        (_state == State::Idle)    ? theme::TEXT_LO :
        (_state == State::Running) ? theme::M_BRIGHT : theme::ACCENT_2;
    drawTitleAndState(stateStr, stateColor);

    // Bracketed counter card.
    const int cardW = 320, cardH = 130;
    const int cardX = theme::CX - cardW / 2;
    const int cardY = 220;
    theme::drawCorners(cardX, cardY, cardW, cardH, theme::M_BRIGHT, 14);

    char buf[16];
    formatElapsed(nowElapsedUs(), buf, sizeof(buf));
    dsp.setTextColor(theme::TEXT_HI, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(4);
    dsp.drawString(buf, theme::CX, cardY + 36);

    if (_lastLapBuf[0]) {
        dsp.setTextColor(theme::M_BRIGHT, theme::BG);
        dsp.setTextSize(2);
        dsp.setTextDatum(textdatum_t::top_center);
        dsp.drawString(_lastLapBuf, theme::CX, cardY + cardH + 14);
    }

    // Exit reminder.
    dsp.setTextColor(theme::TEXT_DIM, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(1);
    dsp.drawString("[A]+[B] to exit", theme::CX, 430);

    // 45° edge hints — rendered INTO the backbuffer so the whole
    // frame pushes as a single shot.
    const bool running = (_state == State::Running);
    theme::drawEdgeHint(95, 95, -45.0f,
                        "[A]", running ? "stop"  : "start",
                        theme::M_BRIGHT, theme::TEXT_LO);
    theme::drawEdgeHint(371, 95, 45.0f,
                        "[B]", running ? "lap"   : "reset",
                        theme::M_BRIGHT, theme::TEXT_LO);

    theme::present();
}
