// shell.cpp — Launcher shell implementation.
//
// Input model: single buttons act on CLICK (release within M5Unified's
// 500 ms hold threshold) — launcher prev/next, or the active app's
// button action. Pressing KEYA+KEYB together is the combo: it enters
// the focused app from the launcher or exits the active app, firing on
// the press overlap, and the constituent presses never leak clicks.
// A single-button hold is a deliberate no-op. A tap enters the focused
// app from the launcher, or is forwarded to the active app as a touch.
// Each app draws into a backbuffer sprite (theme::display()) and calls
// theme::present() once at the end of its draw to push the frame to
// the panel without tearing.

#include "include/shell.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "include/theme.h"

namespace shell {

static App** g_apps      = nullptr;
static size_t g_appCount = 0;

static int   g_focus       = 0;
static App*  g_active      = nullptr;

static constexpr uint32_t TOUCH_DEBOUNCE_MS = 50;
static constexpr int      BEZEL_DEADZONE_PX = 6;
static constexpr int      TAP_TOLERANCE_PX  = 28;

static bool    s_touchActive   = false;
static uint32_t s_touchDownMs  = 0;
static int     s_touchDownX   = 0;
static int     s_touchDownY   = 0;

static void drawLauncher();
static int  clampFocus(int idx);
static bool pointInsideBezel(int x, int y);

void begin(App** apps, size_t count) {
    g_apps = apps;
    g_appCount = count;
    g_focus = 0;
    g_active = nullptr;
    s_touchActive = false;
    drawLauncher();
    theme::present();
}

bool inApp() { return g_active != nullptr; }

void popToLauncher() {
    if (g_active) {
        g_active->onExit();
        g_active = nullptr;
    }
    g_focus = clampFocus(g_focus);
    drawLauncher();
    theme::present();
}

static int clampFocus(int idx) {
    if (g_appCount == 0) return 0;
    idx %= static_cast<int>(g_appCount);
    if (idx < 0) idx += static_cast<int>(g_appCount);
    return idx;
}

void pushEvent(const input::Event& ev) {
    switch (ev.kind) {
        case input::EventKind::Click: {
            if (!g_active) {
                if (ev.button == input::Button::KEYA) {
                    g_focus = clampFocus(g_focus - 1);
                } else {
                    g_focus = clampFocus(g_focus + 1);
                }
                drawLauncher();
                theme::present();
                return;
            }
            g_active->onInput(ev);
            return;
        }

        case input::EventKind::TouchDown: {
            s_touchActive = true;
            s_touchDownMs = millis();
            s_touchDownX  = ev.touch.x;
            s_touchDownY  = ev.touch.y;
            return;
        }

        case input::EventKind::TouchUp: {
            if (!s_touchActive) { return; }
            s_touchActive = false;
            if ((millis() - s_touchDownMs) < TOUCH_DEBOUNCE_MS) { return; }
            int dx = ev.touch.x - s_touchDownX;
            int dy = ev.touch.y - s_touchDownY;
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            if (adx > TAP_TOLERANCE_PX || ady > TAP_TOLERANCE_PX) { return; }
            if (pointInsideBezel(s_touchDownX, s_touchDownY)) return;

            if (!g_active) {
                if (g_appCount == 0) return;
                g_active = g_apps[g_focus];
                g_active->onEnter();
            } else {
                input::Event tap = ev;
                tap.kind = input::EventKind::TouchDown;
                tap.touch.x = s_touchDownX;
                tap.touch.y = s_touchDownY;
                g_active->onInput(tap);
            }
            return;
        }
    }
}

static bool pointInsideBezel(int x, int y) {
    int dx = x - theme::CX;
    int dy = y - theme::CY;
    int r2 = dx * dx + dy * dy;
    int maxR = theme::VISIBLE_R - BEZEL_DEADZONE_PX;
    return r2 > maxR * maxR;
}

// ---- Launcher drawing ------------------------------------------------------

static void drawLauncher() {
    M5GFX& dsp = theme::display();
    dsp.fillScreen(theme::BG);

    if (g_appCount == 0) return;

    // Page indicator.
    char pg[12];
    snprintf(pg, sizeof(pg), "%d / %d",
             g_focus + 1, static_cast<int>(g_appCount));
    dsp.setTextColor(theme::TEXT_DIM, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(2);
    dsp.drawString(pg, theme::CX, 56);

    // Bracketed card frame. Drop the card down so the composition
    // reads top-light, middle-heavy: page indicator at the very
    // top, then the corner hints, then a comfortable gap, then
    // the focused app card in the middle.
    const int cardW = 280, cardH = 160;
    const int cardX = theme::CX - cardW / 2;
    const int cardY = 150;
    theme::drawCorners(cardX, cardY, cardW, cardH, theme::M_BRIGHT, 14);

    App* focused = g_apps[g_focus];

    // App title inside the card.
    dsp.setTextColor(theme::M_BRIGHT, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(3);
    dsp.drawString(focused->name(), theme::CX, cardY + 30);

    dsp.setTextColor(theme::TEXT_LO, theme::BG);
    dsp.setTextSize(2);
    dsp.drawString(focused->subtitle(), theme::CX, cardY + 80);

    // "tap or [A]+[B]" hint just below the card.
    dsp.setTextColor(theme::TEXT_LO, theme::BG);
    dsp.setTextDatum(textdatum_t::top_center);
    dsp.setTextSize(2);
    dsp.drawString("tap or [A]+[B] to enter", theme::CX, cardY + cardH + 18);

    dsp.setTextColor(theme::TEXT_DIM, theme::BG);
    dsp.setTextSize(1);
    dsp.drawString("[A]+[B] to exit", theme::CX, 410);

    // Matrix-style battery bar at the very bottom of the launcher.
    int pct = M5.Power.getBatteryLevel();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int barX = theme::CX - 60;
    int barY = 437;
    theme::drawBatteryBar(barX, barY, pct, "BAT");

    // 45° edge hints rendered into the backbuffer at the same anchors
    // as every app. The launcher's prev/next hint is fixed since the
    // nav buttons (KEYA/KEYB) cycle apps from the launcher itself.
    theme::drawEdgeHint(95, 95, -45.0f,
                        "[A]", "prev",
                        theme::M_BRIGHT, theme::TEXT_LO);
    theme::drawEdgeHint(371, 95, 45.0f,
                        "[B]", "next",
                        theme::M_BRIGHT, theme::TEXT_LO);

    // Push the composed frame to the panel as one shot.
    theme::present();
}

void tick() {
    if (!theme::frameReady()) { return; }
    if (g_active) {
        g_active->onTick();
    }
}
void pumpM5() {
    // --- Buttons: click for actions, A+B overlap for enter/exit ------
    //
    // Single buttons fire on CLICK (M5Unified: release within the 500 ms
    // hold threshold) — never on the down edge — so an A+B combo can
    // never first leak a single-button nav/action. The combo fires on
    // the press overlap itself, once per both-down episode; the
    // constituent releases have their clicks consumed. A single-button
    // hold past the threshold emits no click — a deliberate no-op.
    static bool s_comboArmed = true;
    static bool s_consumeA   = false;
    static bool s_consumeB   = false;

    bool comboEdge =
        (M5.BtnA.wasPressed() && M5.BtnB.isPressed()) ||
        (M5.BtnB.wasPressed() && M5.BtnA.isPressed());

    if (comboEdge && s_comboArmed) {
        s_comboArmed = false;
        s_consumeA = s_consumeB = true;
        if (g_active) {
            popToLauncher();
        } else if (g_appCount > 0) {
            g_active = g_apps[g_focus];
            g_active->onEnter();
        }
    }
    if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed()) {
        s_comboArmed = true;
    }

    if (M5.BtnA.wasClicked()) {
        if (s_consumeA) {
            s_consumeA = false;
        } else {
            input::Event ev; ev.kind = input::EventKind::Click;
            ev.button = input::Button::KEYA;
            pushEvent(ev);
        }
    }
    if (M5.BtnB.wasClicked()) {
        if (s_consumeB) {
            s_consumeB = false;
        } else {
            input::Event ev; ev.kind = input::EventKind::Click;
            ev.button = input::Button::KEYB;
            pushEvent(ev);
        }
    }
    // A combo button held past the click window never emits wasClicked;
    // clear its stale consume flag on any release.
    if (M5.BtnA.wasReleased()) s_consumeA = false;
    if (M5.BtnB.wasReleased()) s_consumeB = false;

    // --- Touch -------------------------------------------------------
    auto t = M5.Touch.getDetail();
    if (t.wasPressed()) {
        input::Event ev; ev.kind = input::EventKind::TouchDown;
        ev.touch.x = t.x; ev.touch.y = t.y;
        pushEvent(ev);
    } else if (t.wasReleased()) {
        input::Event ev; ev.kind = input::EventKind::TouchUp;
        ev.touch.x = t.x; ev.touch.y = t.y;
        pushEvent(ev);
    }
}

}  // namespace shell

