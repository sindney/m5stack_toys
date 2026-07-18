// theme.h — Matrix-movie dark palette + the only primitive the
// launcher/cards actually need: thin L-shaped corner brackets + a
// backbuffer sprite to prevent 30Hz full-screen flicker.

#pragma once

#include <stdint.h>
#include <M5GFX.h>

namespace theme {

// ---- Palette (16-bit RGB565) ------------------------------------------------
// Matrix-movie green-on-black: 0x0000 black, 0x07E0 bright green,
// intermediate green tones for hairlines, amber for UNBALANCED,
// red for danger.
constexpr uint16_t BG        = 0x0000;   // app background (true black)
constexpr uint16_t SURFACE   = 0x0820;   // card surface, very dark green
constexpr uint16_t SURFACE_2 = 0x0C40;   // card hover / selected / pressed
constexpr uint16_t TEXT_HI   = 0x07E0;   // primary text — matrix green
constexpr uint16_t TEXT_LO   = 0x04A0;   // secondary text — medium green
constexpr uint16_t TEXT_DIM  = 0x0200;   // dim captions, hairlines
constexpr uint16_t ACCENT    = 0x07E0;   // primary accent — matrix green
constexpr uint16_t ACCENT_2  = 0xFD20;   // warm amber (UNBALANCED)
constexpr uint16_t DANGER    = 0xF800;   // red
constexpr uint16_t M_FADE    = 0x01A0;   // matrix "fade" green
constexpr uint16_t M_BRIGHT  = 0x07FF;   // cyan-leaning matrix highlight

// ---- Layout -----------------------------------------------------------------
// The C152 StopWatch has a 1.75" round AMOLED (466x466, CO5300 over
// QSPI). The visible face is the inscribed circle of radius 233 from
// the screen centre; everything past that is hidden behind the bezel.
constexpr int SCREEN_W = 466;
constexpr int SCREEN_H = 466;
constexpr int CX       = 233;
constexpr int CY       = 233;
constexpr int VISIBLE_R = 233;
constexpr int BEZEL_DEADZONE_PX = 6;

// ---- Animation --------------------------------------------------------------
// 24 FPS — smooth enough for the bubble / dial updates on Balance
// but slow enough to keep the panel from flickering on this QSPI
// AMOLED. The backbuffer in theme::display() composites each frame
// into a single pushSprite so partial-update tearing doesn't show.
constexpr int  TARGET_FPS      = 24;
constexpr int  FRAME_BUDGET_MS = 1000 / TARGET_FPS;
constexpr bool PERF_MODE_DEFAULT = true;

// ---- Public API -------------------------------------------------------------

// Initialise the theme — allocates the backbuffer sprite and
// sets up the LCD to push from it.
void init();

// Acquire the backbuffer sprite. Apps should draw to this rather
// than M5.Lcd directly. Returns the LGFX_Sprite (compatible with
// M5GFX drawing API).
M5GFX& display();

// Push the current backbuffer to the display. Call once at the end
// of every frame. This is the only place we hit the panel each tick.
void present();

// Frame-rate cap helper. Returns true once per TARGET_FPS window.
bool frameReady();

extern bool perfMode;

// ---- Primitive widgets ------------------------------------------------------

// Four L-shaped corner brackets around the rectangle (x, y, w, h).
void drawCorners(int x, int y, int w, int h,
                 uint16_t color = ACCENT, int len = 14);

// Edge-aligned button hint. Renders `[KEY] label` rotated by
// `angle_deg` and centred on `(anchor_cx, anchor_cy)`. The standard
// usage on the C152 is angle = -45° at (90, 90) and +45° at
// (376, 90), so each label sits on the 45° CCW / CW diagonal of the
// round bezel.
void drawEdgeHint(int anchor_cx, int anchor_cy, float angle_deg,
                  const char* key, const char* label,
                  uint16_t keyColor = ACCENT,
                  uint16_t labelColor = TEXT_LO);

// Matrix-style horizontal battery bar. Renders `pct` (0-100) as a
// row of small filled squares inside a hollow matrix frame. Each
// square = 5% capacity. Position: the (x, y) of the bar's left edge.
// `label` (optional, can be null) is rendered to the right of the
// bar in matrix-green digits.
void drawBatteryBar(int x, int y, int pct, const char* label = nullptr);

}  // namespace theme
