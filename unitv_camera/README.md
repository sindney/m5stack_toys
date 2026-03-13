# UnitV Camera Viewer

A camera streaming system using M5Stack UnitV (K210-based) and M5Stack Core as display.

## Features

- **JPEG Streaming**: Compressed image transfer for efficient bandwidth
- **Handshake Protocol**: Reliable connection establishment between devices
- **FPS Counter**: Real-time frame rate monitoring
- **LED Status**: RGB LED indicates current state (waiting/streaming)
- **Auto-reconnect**: Handles disconnection gracefully

## Hardware Requirements

- **M5Stack UnitV** (K210 AI camera module running MaixPy)
- **M5Stack Core** (ESP32-based with 320x240 display)
- **Grove Cable** connecting UnitV to M5Stack Port C (UART)

## System Architecture

```
┌─────────────────┐    UART (Port C)    ┌─────────────────┐
│   UnitV         │◄───────────────────►│   M5Stack Core  │
│   (Camera)      │    115200 baud      │   (Display)     │
│   boot.py       │                     │   unitv_camera  │
└─────────────────┘                     └─────────────────┘
```

## Communication Protocol

### Handshake Sequence

1. M5Stack sends `M5_READY\n`
2. UnitV responds with `UNITV_OK\n`
3. M5Stack sends `START\n`
4. UnitV begins streaming

### Frame Format

- **Header**: 4 bytes (big-endian) - JPEG data length
- **Data**: JPEG compressed image bytes

## LED Status Indicators (UnitV)

| Color | State |
|-------|-------|
| Red | Waiting for handshake |
| Yellow (blinking) | Handshake in progress |
| Blue | Sending UNITV_OK |
| Green (blinking) | Streaming active |

## Installation

### UnitV Setup

1. Flash MaixPy firmware to UnitV
2. Upload `boot.py` using one of the install scripts:

```bash
# Simple installer
python install_boot_simple.py

# Full installer with options
python install_unitv.py
```

### M5Stack Setup

```batch
compile_upload.bat
```

## Files

| File | Description |
|------|-------------|
| `unitv_camera.ino` | M5Stack Core Arduino firmware |
| `boot.py` | UnitV MaixPy boot script |
| `install_boot_simple.py` | Simple boot.py installer |
| `install_unitv.py` | Full UnitV setup tool |
| `compile_upload.bat` | M5Stack build and upload script |

## Specifications

- **Resolution**: 160x120 (QQVGA)
- **Compression**: JPEG quality 30
- **Baud Rate**: 115200 (UnitV) / 57600 (internal)
- **Buffer Size**: 20KB max per frame
