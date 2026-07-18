// app.h — App base class and event types for the launcher shell.
//
// Each app is a subclass of `App` and is registered in a static array in
// `stopwatch_multiverse.ino`. The shell owns the main loop and dispatches
// lifecycle and input events to the active app.

#pragma once

#include <stdint.h>

namespace input {

// Physical buttons on the StopWatch. The shell assigns meanings (e.g. KEYA
// = left) but the underlying input source is exposed so apps can interpret
// them in their own way.
enum class Button : uint8_t {
    KEYA,    // left
    KEYB,    // right
};

enum class EventKind : uint8_t {
    // Short press (release within M5Unified's 500 ms hold threshold),
    // dispatched at release by the shell. Raw button edges never reach
    // apps: the A+B enter/exit combo owns the press overlap, and a
    // single-button hold is a deliberate no-op.
    Click,
    TouchDown,   // raw touch event; coordinates are in touch.x / touch.y
    TouchUp,
};

struct Touch {
    int x;
    int y;
};

struct Event {
    EventKind kind;
    union {
        Button button;
        Touch  touch;
    };
};

}  // namespace input

// Identifier used by the launcher to draw the carousel. The order matches
// the order in `g_apps` in stopwatch_multiverse.ino. VoiceToy and
// Compass are intentionally absent — this is a four-app firmware.
enum class AppId : uint8_t {
    Stopwatch = 0,
    Balance   = 1,
    Badge     = 2,
    UsbDrive  = 3,
};

class App {
public:
    virtual ~App() = default;

    // Lifecycle hooks.
    virtual void onEnter()  = 0;
    virtual void onTick()   = 0;     // non-blocking; must return < 16 ms
    virtual void onInput(const input::Event& ev) = 0;
    virtual void onExit()   = 0;

    // Display name (used by the launcher).
    virtual const char* name() const = 0;

    // Single-line tagline shown beneath the name in the launcher and
    // inside each app's title block. Kept short (< 24 chars).
    virtual const char* subtitle() const = 0;

    // Accent colour for app-specific accents that shouldn't use the
    // theme's ACCENT (e.g. the Balance app uses ACCENT_2 for
    // UNBALANCED). Apps may return any palette token.
    virtual uint16_t iconColor() const = 0;
};