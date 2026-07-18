#!/usr/bin/env bash
#
# flash.sh — Build and upload the stopwatch_multiverse firmware.
#
# Idempotent: re-running with the device connected re-flashes the latest
# sources. With --no-upload, only the compile step runs (useful for CI
# smoke tests on a host without a connected StopWatch).
#
# Requirements:
#   * arduino-cli on PATH, with the m5stack:esp32 core (3.3.x) and the
#     M5Unified / M5GFX / M5IOE1 libraries already installed.
#
# Usage:
#   scripts/flash.sh                     # build + upload
#   scripts/flash.sh --no-upload         # build only
#   scripts/flash.sh --port COM7         # upload to explicit serial port
#
# Badge images are NOT flashed by this script — open the USB DRIVE app on
# the watch and use tools/badge_uploader.html (or plain drag-and-drop).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH="${ROOT}/stopwatch_multiverse.ino"
FQBN="m5stack:esp32:m5stack_stopwatch:UploadSpeed=921600,USBMode=default,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default"

DO_UPLOAD=1
SERIAL_PORT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-upload)    DO_UPLOAD=0 ;;
    --port)         SERIAL_PORT="$2"; shift ;;
    -h|--help)
      grep '^#' "${BASH_SOURCE[0]}" | sed -e 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "Unknown flag: $1" >&2; exit 2 ;;
  esac
  shift
done

# ---- Build -----------------------------------------------------------------

echo ">>> Compiling ${SKETCH}"
arduino-cli compile \
  --fqbn "${FQBN}" \
  --build-property "build.partitions=default_16MB" \
  "${SKETCH}"

# ---- Upload (optional) ------------------------------------------------------

if [[ "${DO_UPLOAD}" == "1" ]]; then
  echo ">>> Detecting serial port"
  if [[ -z "${SERIAL_PORT}" ]]; then
    # Pick the first port whose VID is Espressif's (303A).
    # The running app is a TinyUSB composite (CDC console + MSC card
    # reader); download mode is the ROM's USB Serial/JTAG — both 303A.
    # arduino-cli ≥ 1.x shape: detected_ports[].port.{address,label,
    # properties.vid}; the VID is in properties, not the label.
    SERIAL_PORT="$(arduino-cli board list --format json \
      | python -c '
import json, sys
data = json.load(sys.stdin)
for entry in data.get("detected_ports", []):
    port = entry.get("port") or {}
    if not isinstance(port, dict):
        continue
    props = port.get("properties") or {}
    vid = str(props.get("vid") or "").upper()
    label = str(port.get("label") or "").upper()
    if "303A" in vid or "303A" in label or "M5STACK" in label:
        print(port.get("address") or ""); sys.exit(0)
print("")
' || true)"
  fi
  if [[ -z "${SERIAL_PORT}" ]]; then
    echo "Could not auto-detect a serial port. Plug the StopWatch in or pass --port." >&2
    exit 3
  fi

  echo ">>> Uploading to ${SERIAL_PORT}"
  arduino-cli upload \
    --fqbn "${FQBN}" \
    --port "${SERIAL_PORT}" \
    "${SKETCH}" || {
      echo "Upload failed." >&2
      echo "The TinyUSB app reboots into download mode on a 1200 bps port" >&2
      echo "touch (pyserial: open the port at 1200 baud, close, wait 3 s," >&2
      echo "then re-run). If that fails too, hold the reset button ~2 s" >&2
      echo "until the green LED lights, then retry." >&2
      exit 4
    }
fi
