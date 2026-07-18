## 1. Shell input model

- [x] 1.1 `include/app.h`: `EventKind` — replace `ButtonDown`/`ButtonUp`
      with `Click` (short press decided at release); comment the model.
- [x] 1.2 `src/shell.cpp` `pumpM5()`: A+B combo fires on press overlap
      (`wasPressed && other.isPressed`), armed once per both-down
      episode; per-button consume flags suppress the constituent
      clicks; single holds are no-ops; touch path unchanged.
- [x] 1.3 `pushEvent()`: `Click` replaces `ButtonDown` (launcher nav +
      app dispatch); drop `ButtonUp`; remove dead `drawOverlayHints`,
      `DOUBLE_MS`, `s_lastKeyMs`, `s_lastKeyBtn`.
- [x] 1.4 Launcher strings: "tap or [A]+[B] to enter",
      "[A]+[B] to exit". Edge hints stay (still click-accurate).

## 2. Apps

- [x] 2.1 `app_badge.cpp`: fullscreen `draw()` (edge-to-edge blit; drop
      title, slot counter, exit reminder, edge hints; keep empty state
      + `[PAUSED]` chip); `onInput` case → `Click`.
- [x] 2.2 `app_stopwatch.cpp`: `onInput` case → `Click`; exit-hint
      string updated.
- [x] 2.3 `app_usb_drive.cpp`: exit-hint string updated.
- [x] 2.4 `stopwatch_multiverse.ino` header comment + `README.md`
      exit wording.

## 3. Verify

- [x] 3.1 `scripts/flash.sh --no-upload` compiles, 0 errors/warnings.
- [x] 3.2 Hardware pass: click A/B nav without accidental exits; A+B
      enters/exits without leaking a nav; badge is fullscreen; stopwatch
      still starts/stops on click.
