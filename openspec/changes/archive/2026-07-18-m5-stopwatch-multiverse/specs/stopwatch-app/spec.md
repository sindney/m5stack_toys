## MODIFIED Requirements

The Stopwatch app's behaviour is unchanged from the previous version;
only the visual layout moves to the flat theme.

### Requirement: Timing primitives

The Stopwatch app SHALL provide `start`, `stop`, `lap`, and `reset`
operations. The elapsed time SHALL be measured using
`esp_timer_get_time()` and SHALL have at least 10 ms precision.

#### Scenario: Start and stop

- **WHEN** the user starts the stopwatch and then stops it after
  12.345 s
- **THEN** the displayed elapsed time reads `00:00:12.345` within
  ±10 ms

#### Scenario: Lap capture

- **WHEN** the user captures a lap while the stopwatch is running
- **THEN** the current elapsed time is appended to a lap list and
  the main counter continues without resetting

### Requirement: Display format

The Stopwatch app SHALL display elapsed time as `HH:MM:SS.mmm` in
the large mono-digit display font, centred on the AMOLED round
screen below the title, with the most recent lap below the main
counter.

#### Scenario: Long elapsed time

- **WHEN** the elapsed time exceeds one hour
- **THEN** the display continues in `HH:MM:SS.mmm` format without
  truncation up to `99:59:59.999`

### Requirement: Input mapping

The Stopwatch app SHALL map inputs as follows: KEYA = start / stop
toggle, KEYB = lap (when running) / reset (when stopped), screen
tap = start / stop toggle. The action labels shown in the button
hint footer SHALL swap based on the current state.

#### Scenario: Reset when stopped

- **WHEN** the stopwatch is stopped and the user presses KEYB
- **THEN** the elapsed time and lap list are cleared and the display
  reads `00:00:00.000`

### Requirement: Button hint footer

The Stopwatch app SHALL always render a small button hint at the
bottom of the screen showing the two physical buttons and their
current action (`KEYA` / `Start`, `KEYB` / `Lap` while running;
`KEYA` / `Start`, `KEYB` / `Reset` when stopped).

#### Scenario: Hint matches state

- **WHEN** the Stopwatch transitions between Idle, Running, and
  Stopped
- **THEN** the button hint footer labels update immediately to
  reflect the new state's actions
