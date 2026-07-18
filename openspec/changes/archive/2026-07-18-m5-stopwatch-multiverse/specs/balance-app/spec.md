## MODIFIED Requirements

The Balance app's tilt-detection behaviour is unchanged from the
previous version; only the visual layout moves to the flat theme.

### Requirement: Tilt measurement

The Balance app SHALL read the BMI270 accelerometer at ≥ 50 Hz and
compute the tilt angle as the angle between the gravity vector and
the device's local +Z axis. A reading of 0° SHALL mean the device
face is parallel to the ground (screen up).

#### Scenario: Flat on table

- **WHEN** the device lies flat on a horizontal surface
- **THEN** the measured tilt reads 0° ± 1°

#### Scenario: Tilted

- **WHEN** the device is tilted 30° around the X axis
- **THEN** the measured tilt reads 30° ± 2°

### Requirement: Balanced envelope

The Balance app SHALL declare the device `BALANCED` when the
absolute tilt is below a configurable threshold (default 2°) and
`UNBALANCED` otherwise. The state MUST update at least 10 times per
second.

#### Scenario: Cross threshold

- **WHEN** the tilt rises from 1° to 3°
- **THEN** the status changes from `BALANCED` to `UNBALANCED` within
  100 ms

### Requirement: Feedback

While `UNBALANCED`, the Balance app SHALL pulse the vibration motor
at 2 Hz on M5IOE1 PWM channel 9, and the on-screen bubble indicator
SHALL show the offset of the gravity vector from the device centre,
scaled to fit the inner dial circle.

#### Scenario: Vibration during imbalance

- **WHEN** the device is `UNBALANCED` for more than 500 ms
- **THEN** the vibration motor pulses at approximately 2 Hz

#### Scenario: Vibration stops when balanced

- **WHEN** the device returns to within the balanced envelope
- **THEN** the vibration motor stops within one pulse cycle
  (≤ 500 ms)

### Requirement: Direction hint

The Balance app SHALL render a short bottom-of-screen hint
("`tilt the watch`" while balanced) and shall display the current
tilt in degrees plus the `BALANCED` / `UNBALANCED` label below the
dial.

#### Scenario: UNBALANCED label

- **WHEN** the device is tilted past the 2° threshold
- **THEN** the on-screen label reads `UNBALANCED` and the dial
  border shifts to `ACCENT_2`

### Requirement: IMU ready handshake

The Balance app SHALL flip its internal `_imuReady` flag on the
**first** successful `M5.Imu.getAccel()` call so that the "IMU
OFFLINE" toast is only shown when no reading has ever succeeded.

#### Scenario: First successful read

- **WHEN** the app calls `M5.Imu.getAccel()` and the call returns
  valid data
- **THEN** `_imuReady` SHALL be true for the remainder of the app's
  lifetime
