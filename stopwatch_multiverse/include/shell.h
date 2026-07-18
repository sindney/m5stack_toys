// shell.h — Public interface of the launcher shell.
//
// The shell owns the main loop, the carousel focus index, the
// click-vs-combo button classifier (click = action, A+B overlap =
// enter/exit) and the touch debounce. Apps register themselves in a
// static array and the shell dispatches events to the active one.

#pragma once

#include <stddef.h>

#include "app.h"

namespace shell {

// Bind the static app registry and bring up the launcher. Call once from
// setup() after theme::init().
void begin(App** apps, size_t count);

// Drive one frame of the shell + active app. Call from loop().
void tick();

// Force the shell back to the launcher (used by the A+B combo and by
// individual apps that want to exit).
void popToLauncher();

// True while an app is on-screen (as opposed to the launcher).
bool inApp();

// Push an input event into the shell. The shell dispatches it to the
// active app or, for clicks at the launcher, moves the carousel focus.
void pushEvent(const input::Event& ev);

// Read M5 buttons + touch and forward them to pushEvent(). The sketch
// calls this from loop() before tick() so the shell sees fresh input.
void pumpM5();

}  // namespace shell