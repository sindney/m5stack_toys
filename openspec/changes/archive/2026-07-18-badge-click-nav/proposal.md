---
schema: spec-driven
created: 2026-07-18
---

## Why

The launcher's button model dispatches on the press *edge*: a
`ButtonDown` fires the instant KEYA/KEYB goes low, and the A+B
enter/exit combo is detected when the second button edges while the
first is held. Two consequences:

- Pressing A+B with any skew first leaks a single-button nav (focus
  moves or the badge image advances) and only then exits — the combo
  races the edge dispatch.
- Single-button holds still fire the press edge, so there is no way to
  touch a button deliberately without triggering its action.

M5Unified's `Button_Class` already distinguishes a click (release
within the 500 ms hold threshold) from a hold (`wasClicked()` /
`wasHold()`), so the shell can move single-button actions to
click-at-release and let the combo own the press overlap — making both
gestures race-free.

While in the Badge app, the slideshow draws a title, slot counter,
exit reminder, and corner hints around the 466×466 image — chrome that
a worn badge does not need. The slot files are already the full panel
frame, pre-masked to the round face by the host tool.

## What Changes

- **BREAKING (internal)** `input::EventKind::ButtonDown` / `ButtonUp`
  are replaced by `Click`. The shell no longer forwards raw button
  edges to apps; it dispatches one `Click` per short press, decided at
  release (M5Unified hold threshold, 500 ms). Apps switch their
  `onInput` cases; touch events are unchanged.
- **Shell input**: single-button actions (launcher prev/next, badge
  prev/next image, stopwatch start/stop/lap/reset) fire on click. The
  A+B combo (enter app from launcher, exit app to launcher) fires on
  the press overlap, at most once per both-buttons-down episode, and
  suppresses the constituent clicks. A single-button hold (> 500 ms)
  is a deliberate no-op.
- **Badge app**: fullscreen rendering — the slot image is blitted
  edge-to-edge with no title, slot counter, exit reminder, or edge
  hints. The empty state keeps a minimal "no images" message, and a
  small `[PAUSED]` chip still appears while the slideshow is paused
  (otherwise the tap-to-pause gesture has no visible feedback).
- Launcher hint strings updated to the new model ("tap or [A]+[B] to
  enter", "[A]+[B] to exit"). Dead code removed from `shell.cpp`
  (unused `drawOverlayHints`, `DOUBLE_MS`, `s_lastKeyMs`,
  `s_lastKeyBtn`).

## Capabilities

### New Capabilities

- `shell-input`: the click-vs-combo button model — click dispatch at
  release, combo on press overlap, click suppression, hold no-op.

### Modified Capabilities

- *(none — `openspec/specs/` is empty; nothing to delta against.)*

## Impact

- **Firmware modified**: `include/app.h` (EventKind), `src/shell.cpp`
  (pumpM5 state machine, pushEvent, launcher strings, dead code),
  `src/app_badge.cpp` (Click case, fullscreen draw),
  `src/app_stopwatch.cpp` (Click case), `src/app_usb_drive.cpp` (exit
  hint string), `stopwatch_multiverse.ino` (header comment),
  `README.md` (exit wording).
- **Firmware deleted**: none.
- **Behavioural**: stopwatch start/stop now registers at button release
  instead of press (sub-100 ms for a normal tap; consistent with the
  rest of the UI).
- **Out of scope**: double-click gestures, per-app hold actions, touch
  changes.
