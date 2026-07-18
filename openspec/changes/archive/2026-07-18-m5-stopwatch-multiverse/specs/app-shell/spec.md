## MODIFIED Requirements

The previous launcher carousel and five-app ring layout are replaced
by a three-card circular launcher. Requirements carry over where the
behaviour is unchanged.

### Requirement: Three-card circular launcher

The system SHALL display a launcher on the round AMOLED that shows
exactly three large round-cornered cards. The focused app SHALL be
rendered as the largest card centred on the face; the previous and
next apps SHALL be rendered as smaller cards to the immediate left
and right of the focused card, partially visible past the bezel.

#### Scenario: Boot into launcher

- **WHEN** the device powers on
- **THEN** the launcher is shown within 3 seconds with the first app
  in the registration order focused

#### Scenario: Navigate with KEYA / KEYB

- **WHEN** the user short-presses KEYA (left) or KEYB (right)
- **THEN** the focus moves to the previous or next app respectively,
  wrapping around at either end

#### Scenario: Navigate with swipe

- **WHEN** the user performs a horizontal swipe ≥ 80 px over ≥ 200 ms
- **THEN** the focus moves once in the swipe direction and no tap
  is fired for the same gesture

### Requirement: Tap to enter

The system SHALL launch the focused app when the user performs a
single tap on the touch screen.

#### Scenario: Enter focused app

- **WHEN** an app is focused in the launcher and the user taps the
  screen
- **THEN** the app's `onEnter` lifecycle hook runs and the app
  takes over rendering

### Requirement: Double-press to return

The system SHALL return to the launcher when the user double-presses
either KEYA or KEYB within 350 ms.

#### Scenario: Pop back to launcher

- **WHEN** an app is active and the user double-presses KEYA or
  KEYB within 350 ms
- **THEN** the active app's `onExit` lifecycle hook runs and the
  three-card launcher reappears with the same app still focused

#### Scenario: Single press is not exit

- **WHEN** an app is active and the user single-presses KEYA or
  KEYB
- **THEN** the app remains active and receives the press as a
  normal input

### Requirement: App lifecycle contract

The system SHALL define each app as a class implementing `onEnter`,
`onTick`, `onInput`, and `onExit`. The shell SHALL own the main loop
and dispatch calls to the active app, never blocking longer than one
frame.

#### Scenario: Non-blocking tick

- **WHEN** an app's `onTick` is called
- **THEN** it returns within 16 ms so the shell can maintain a
  30+ FPS frame rate

#### Scenario: Input dispatch

- **WHEN** KEYA, KEYB, or a touch event occurs while an app is
  active
- **THEN** the shell debounces the event (60 ms minimum press for
  touch, 8 px bezel deadzone) and forwards it to the active
  app's `onInput`

### Requirement: Persistent app registry

The system SHALL register exactly three apps (Stopwatch, Balance,
Badge) in a static array and present them in a fixed order so the
launcher is deterministic across boots.

#### Scenario: Order is stable

- **WHEN** the device boots multiple times
- **THEN** the apps appear in the same order: Stopwatch, Balance,
  Badge
