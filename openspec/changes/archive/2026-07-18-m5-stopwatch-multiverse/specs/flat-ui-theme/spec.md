## ADDED Requirements

This spec replaces the previous `cyberpunk-ui-theme` spec and
describes the flat dark theme used by the three-app rewrite.

### Requirement: Single shared flat palette

The system SHALL expose a single flat dark color palette as named
RGB565 constants: `BG`, `SURFACE`, `SURFACE_2`, `TEXT_HI`, `TEXT_LO`,
`ACCENT`, `ACCENT_2`, `DANGER`. All apps MUST source their colours
from this palette and MUST NOT define new colours locally. **All
constants are `uint16_t`** — 24-bit RGB888 literals are silently
truncated by the C compiler and would render near-black, which is
the bug that killed the neon-palette launch.

| Token       | RGB565 (uint16_t) | Use                              |
| ----------- | ----------------- | -------------------------------- |
| `BG`        | `0x0000`          | app background (true black)      |
| `SURFACE`   | `0x1A1A`          | card surface, dark grey          |
| `SURFACE_2` | `0x2A2A`          | card hover / selected            |
| `TEXT_HI`   | `0xFFFF`          | primary text (white)             |
| `TEXT_LO`   | `0xB0B0`          | secondary text                   |
| `ACCENT`    | `0x5BD0`          | accent (calm cyan, not neon)     |
| `ACCENT_2`  | `0xFF8A4D`        | warm accent (peach)              |
| `DANGER`    | `0xF05050`        | warning / imbalance / record dot |

#### Scenario: No ad-hoc colours

- **WHEN** a new app is added
- **THEN** it includes `theme.h` and uses only the eight palette
  constants for any colour value

### Requirement: Primitive widgets

The system SHALL provide the following reusable widgets:
`drawCard`, `drawHint`, and `drawButtonHint`. Apps MUST compose
these primitives rather than drawing raw pixels or text directly
where a primitive exists. The cyberpunk primitives
(`drawRingSelector`, `drawStatusBar`, `drawNeonButton`, `drawToast`,
`drawNeonLabel`) SHALL NOT be used by any new code and are slated
for removal.

#### Scenario: Card on launcher

- **WHEN** the launcher renders the focused app
- **THEN** it uses `drawCard` so the card style is identical to
  in-app cards (Badge framing, Balance dial background)

### Requirement: Typography

The system SHALL load exactly two fonts: a small caption font and a
large mono-digit display font. Apps MUST reuse the registered theme
fonts and MUST NOT load their own font. The large font is sized so
that `HH:MM:SS.mmm` fits comfortably inside the focused card.

#### Scenario: Consistent fonts

- **WHEN** any app draws text
- **THEN** it uses one of the two registered theme fonts and no
  app loads its own font

### Requirement: Animation budget

The system SHALL cap full-screen redraws at 30 FPS and SHALL gate
any animation effect behind a `theme.perfMode` flag that defaults to
enabled.

#### Scenario: Frame budget

- **WHEN** an app calls `onTick` rapidly
- **THEN** the shell throttles rendering so the AMOLED refresh rate
  does not exceed 30 FPS

### Requirement: No global status bar

The system SHALL NOT draw a top-of-screen status bar. Each app owns
its own header (a centred title in `TEXT_HI`) and its own bottom
button hint footer. There is no `drawStatusBar` primitive; the
battery percentage is not shown on app screens.

#### Scenario: No battery chrome

- **WHEN** any app is active
- **THEN** no top status bar is drawn over the app's content
