# shell-input Specification

## Purpose
TBD - created by archiving change badge-click-nav. Update Purpose after archive.
## Requirements
### Requirement: Click-based single-button actions
The shell SHALL dispatch a single-button action only after a full click
— press followed by release within M5Unified's hold threshold
(default 500 ms), surfaced as `input::EventKind::Click`. Raw press and
release edges SHALL NOT be dispatched to apps. A press held beyond the
hold threshold and released SHALL produce no event.

#### Scenario: Click navigates the launcher
- **WHEN** the launcher is showing and the user presses and releases
  KEYB within 500 ms
- **THEN** the focused app advances by one, exactly once, at release.

#### Scenario: Hold is a no-op
- **WHEN** the user holds KEYA for more than 500 ms and releases
- **THEN** no launcher navigation and no app action occurs.

### Requirement: A+B combo enter/exit
The shell SHALL enter the focused app (from the launcher) or exit the
active app (from anywhere) when KEYA and KEYB are simultaneously
pressed, firing on the press overlap itself, at most once per
both-buttons-down episode (re-armed only when both buttons are
released). The constituent button presses SHALL NOT also produce
clicks, regardless of which button went down first or how quickly the
pair is released.

#### Scenario: No leaked navigation on exit
- **WHEN** the Badge app is showing slot 2 and the user presses KEYA
  then KEYB 80 ms later, then releases both
- **THEN** the shell pops to the launcher and the badge never advances
  to another slot.

#### Scenario: Combo re-arms only after full release
- **WHEN** the user holds both buttons, releases KEYA only, and presses
  KEYA again while KEYB is still held
- **THEN** no second enter/exit fires until both buttons have been
  released and a new overlap begins.

### Requirement: Fullscreen badge rendering
The Badge app SHALL blit the 466×466 slot image edge-to-edge with no
title, slot counter, exit reminder, or corner hints. When no valid
slots exist it SHALL show a minimal empty-state message, and while the
slideshow is paused it SHALL overlay a small pause indicator.

#### Scenario: Worn badge shows only the image
- **WHEN** the Badge app displays a valid slot
- **THEN** every visible pixel comes from the slot image (except during
  the paused state, which shows the `[PAUSED]` chip).

