// theme.cpp — Matrix-movie palette + backbuffer sprite + corner /
// hint / battery primitives.

#include "include/theme.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <lgfx/v1/LGFX_Sprite.hpp>

namespace theme {

bool perfMode = PERF_MODE_DEFAULT;

static uint32_t s_lastFrameMs = 0;

namespace {
LGFX_Sprite s_frame;
bool s_frameReady = false;
}

void init() {
    s_frame.setColorDepth(16);
    if (!s_frame.createSprite(SCREEN_W, SCREEN_H)) {
        // Fall back to direct-LCD if PSRAM allocation fails.
        s_frameReady = false;
        M5.Lcd.fillScreen(BG);
        M5.Lcd.setTextColor(TEXT_HI, BG);
        M5.Lcd.setTextDatum(textdatum_t::top_left);
        return;
    }
    s_frameReady = true;
    s_frame.fillScreen(BG);
    s_frame.setTextColor(TEXT_HI, BG);
    s_frame.setTextDatum(textdatum_t::top_left);
    s_lastFrameMs = millis();
}

M5GFX& display() {
    // s_frame is an LGFX_Sprite, which inherits from LGFXBase just
    // like M5GFX. The drawing API (fillScreen, drawString, etc.) is
    // shared, so we return the sprite by reference. When the back-
    // buffer allocation failed at boot, fall back to the panel.
    if (s_frameReady) {
        // s_frame is a member variable of type LGFX_Sprite; treat it
        // as M5GFX& for the API the apps use. This works because the
        // M5GFX class derives from the same LGFXBase as LGFX_Sprite.
        return reinterpret_cast<M5GFX&>(s_frame);
    }
    return M5.Lcd;
}

void present() {
    if (s_frameReady) {
        s_frame.pushSprite(0, 0);
    }
}

bool frameReady() {
    uint32_t now = millis();
    if (static_cast<int32_t>(now - s_lastFrameMs) >= FRAME_BUDGET_MS) {
        s_lastFrameMs = now;
        return true;
    }
    return false;
}

void drawCorners(int x, int y, int w, int h,
                 uint16_t color, int len) {
    M5GFX& dsp = display();
    dsp.drawFastHLine(x,            y,            len, color);
    dsp.drawFastVLine(x,            y,            len, color);
    dsp.drawFastHLine(x + w - len,  y,            len, color);
    dsp.drawFastVLine(x + w - 1,    y,            len, color);
    dsp.drawFastHLine(x,            y + h - 1,    len, color);
    dsp.drawFastVLine(x,            y + h - len,  len, color);
    dsp.drawFastHLine(x + w - len,  y + h - 1,    len, color);
    dsp.drawFastVLine(x + w - 1,    y + h - len,  len, color);
}

void drawEdgeHint(int anchor_cx, int anchor_cy, float angle_deg,
                  const char* key, const char* label,
                  uint16_t keyColor, uint16_t labelColor) {
    char combined[40];
    snprintf(combined, sizeof(combined), "%s %s", key, label);

    constexpr int CHAR_W = 12;       // size 2
    int textW = static_cast<int>(strlen(combined)) * CHAR_W;
    constexpr int TEXT_H = 16;

    // Render the hint INTO the backbuffer (theme::display()) so it
    // ships as part of the same frame the rest of the UI is on. No
    // separate partial-push to the panel = no "blinking" hint area.
    M5GFX& target = display();

    LGFX_Sprite spr(&target);
    spr.setColorDepth(16);
    if (!spr.createSprite(textW, TEXT_H)) return;
    spr.fillScreen(BG);
    spr.setTextDatum(textdatum_t::top_left);
    spr.setTextSize(2);

    spr.setTextColor(keyColor, BG);
    int keyPx = static_cast<int>(strlen(key)) * CHAR_W;
    spr.drawString(key, 0, 0);

    spr.setTextColor(labelColor, BG);
    spr.drawString(label, keyPx + 6, 0);

    spr.pushRotateZoom(anchor_cx, anchor_cy, angle_deg,
                       1.0f, 1.0f, BG);
    spr.deleteSprite();
}

void drawBatteryBar(int x, int y, int pct, const char* label) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    M5GFX& dsp = display();

    constexpr int CELL = 4;          // 4×4 px per cell
    constexpr int GAP  = 2;
    constexpr int N   = 20;          // 20 cells = 5% per cell

    int filled = (pct * N + 50) / 100;

    int barW = N * (CELL + GAP) - GAP;
    int barH = CELL;

    // Hollow matrix frame in dim green.
    dsp.drawRect(x, y, barW + 4, barH + 4, TEXT_DIM);
    dsp.drawRect(x + 1, y + 1, barW + 2, barH + 2, BG);

    uint16_t cellColor = (pct < 20) ? DANGER : (pct < 40) ? ACCENT_2 : M_BRIGHT;
    for (int i = 0; i < N; ++i) {
        int cx = x + 2 + i * (CELL + GAP);
        if (i < filled) {
            dsp.fillRect(cx, y + 2, CELL, CELL, cellColor);
        } else {
            dsp.drawRect(cx, y + 2, CELL, CELL, TEXT_DIM);
        }
    }

    if (label) {
        dsp.setTextColor(TEXT_LO, BG);
        dsp.setTextDatum(textdatum_t::top_left);
        dsp.setTextSize(1);
        dsp.drawString(label, x + barW + 12, y + 2);
    }

    // Numeric percentage after the bar.
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%3d%%", pct);
    dsp.setTextColor(pct < 20 ? DANGER : M_BRIGHT, BG);
    dsp.setTextSize(1);
    dsp.setTextDatum(textdatum_t::top_right);
    dsp.drawString(pctStr, x + barW + 12 + 60, y + 2);
}

}  // namespace theme
