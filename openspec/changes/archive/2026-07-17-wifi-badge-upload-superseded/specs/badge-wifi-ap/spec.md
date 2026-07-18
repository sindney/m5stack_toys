## ADDED Requirements

### Requirement: Wi-Fi Upload app on the launcher
The `stopwatch_multiverse` firmware SHALL expose a fourth app on its
launcher named "WiFi Upload". Selecting it from the launcher SHALL
enter the app; holding KEYA+KEYB together SHALL return to the
launcher.

#### Scenario: App appears in the launcher
- **WHEN** the launcher screen is shown
- **THEN** four app tiles are visible, the fourth labelled "WiFi
  Upload" (or the localized equivalent), and the existing three
  apps (Stopwatch, Balance, Badge) are unchanged.

#### Scenario: Selecting the app
- **WHEN** the user taps the "WiFi Upload" tile
- **THEN** the launcher is hidden, the app's UI is shown, and the
  firmware starts bringing up a 2.4 GHz access point.

### Requirement: Open SoftAP on a deterministic channel
On entering the Wi-Fi Upload app, the firmware SHALL configure the
ESP32-S3 in SoftAP mode with an open network (no password) named
`StopWatch-XXXX` where `XXXX` is the last 4 hex digits of the
chip's MAC address. The AP SHALL be on channel 6 with 20 MHz
bandwidth and the maximum supported TX power. The SoftAP gateway
address SHALL be `192.168.4.1` and the DHCP server SHALL hand out
addresses on `192.168.4.0/24`.

#### Scenario: AP appears within 3 seconds
- **WHEN** the user enters the Wi-Fi Upload app
- **THEN** within 3 seconds the AMOLED displays the AP SSID
  (`StopWatch-XXXX`) and the gateway IP (`192.168.4.1`).

#### Scenario: Host can join
- **WHEN** the host scans for Wi-Fi networks
- **THEN** the network `StopWatch-XXXX` (open) is listed and a
  successful join yields an IP on `192.168.4.0/24` (typically
  `192.168.4.2`).

### Requirement: HTTP API on the SoftAP gateway
The firmware SHALL run a small HTTP server on `192.168.4.1:80` that
serves the following endpoints:

| Endpoint | Method | Body | Success reply |
| --- | --- | --- | --- |
| `/` | GET | – | `200` HTML status page |
| `/api/slots` | GET | – | `200 application/json` with `{"sizes":[n0,n1,...]}` |
| `/api/upload?slot=N` | POST | raw RGB565 LE bytes | `200 OK` |
| `/api/erase?slot=N` | POST | – | `200 OK` |

The server SHALL listen on port 80 (the default HTTP port) so the
host CLI can use `http://192.168.4.1/...` URLs without an explicit
port.

#### Scenario: Status page
- **WHEN** the host visits `http://192.168.4.1/`
- **THEN** a 200 response with a small HTML body listing the
  firmware version, the active AP SSID, the current slot count,
  and the number of bytes free on the badge partition.

#### Scenario: List slots
- **WHEN** the host sends `GET /api/slots`
- **THEN** the firmware reads `/badge/slot_<N>.bin` sizes for
  `N = 0..31` (zero for missing) and replies with
  `{"sizes":[0, 434472, 0, ...]}` in slot order.

#### Scenario: Upload to an empty slot
- **WHEN** the host sends `POST /api/upload?slot=2` with a 434 472
  byte body
- **THEN** the firmware writes the bytes to `/badge/slot_2.bin`
  and replies `200 OK` within 2 seconds.

#### Scenario: Upload to a slot with the wrong size
- **WHEN** the host sends `POST /api/upload?slot=0` with a body
  whose `Content-Length` is not 434 472
- **THEN** the firmware replies `400 bad_size` and writes nothing
  to the file system.

#### Scenario: Upload to a slot out of range
- **WHEN** the host sends `POST /api/upload?slot=99`
- **THEN** the firmware replies `400 slot_range` and writes nothing.

#### Scenario: Erase a slot
- **WHEN** the host sends `POST /api/erase?slot=3`
- **THEN** the firmware deletes `/badge/slot_3.bin` (if it exists)
  and replies `200 OK`. If the slot is already empty the reply is
  still `200 OK`.

### Requirement: AMOLED status display
While the app is open, the AMOLED SHALL show the AP SSID, the
gateway IP, the slot count, and the most recent operation's
outcome. The display SHALL refresh whenever the slot count changes
or a transfer completes.

#### Scenario: Idle display
- **WHEN** no host is connected and no transfer is in progress
- **THEN** the AMOLED shows
  `WiFi: StopWatch-XXXX\nIP: 192.168.4.1\nSlots: N / 32`.

#### Scenario: Transfer in progress
- **WHEN** a `POST /api/upload` is being processed
- **THEN** the AMOLED shows a "Uploading…" indicator and the slot
  number being written, replacing the idle state.

#### Scenario: Transfer complete
- **WHEN** the upload finishes
- **THEN** the AMOLED shows `Uploaded slot N (434472 bytes)` for
  3 seconds, then returns to the idle display.

### Requirement: Watch returns to launcher cleanly
Exiting the app (KEYA+KEYB hold) SHALL tear down the HTTP server,
stop the DHCP server, and bring the radio back to idle before
returning to the launcher. Re-entering the app SHALL bring the AP
back up.

#### Scenario: Exit tears down the AP
- **WHEN** the user exits the app
- **THEN** within 2 seconds the AP `StopWatch-XXXX` is no longer
  visible to other clients and the launcher screen is shown.
