# AtomS3R PC Monitor

A BLE-based PC hardware monitor display for M5Stack AtomS3R.

## Features

- **BLE Connection**: Acts as a BLE server waiting for PC client connection
- **Real-time Stats**: Displays CPU, GPU, and Memory usage percentages
- **Temperature Display**: Shows CPU and GPU temperatures
- **History Graph**: Line chart showing usage trends over time
- **Tech Green UI**: Sci-fi styled green theme interface
- **Screen Rotation**: Tap button to rotate screen orientation

## Hardware Requirements

- **M5Stack AtomS3R** (ESP32-S3 based with 128x128 display)
- **PC with Bluetooth** running the companion Python script

## System Architecture

```
┌─────────────────┐         BLE          ┌─────────────────┐
│   AtomS3R       │◄────────────────────►│   PC            │
│   (BLE Server)  │                      │   pc_monitor.py │
└─────────────────┘                      └─────────────────┘
```

## Usage

1. Flash the firmware to AtomS3R
2. Run `pc_monitor.py` on your PC
3. The Python script will automatically discover and connect to the device
4. PC stats will be displayed in real-time on the AtomS3R screen

## Build & Upload

```batch
# Install Python dependencies first
install_deps.bat

# Build and upload firmware
build_upload.bat
```

## Files

| File | Description |
|------|-------------|
| `atoms3r_pc_monitor.ino` | Arduino firmware for AtomS3R |
| `pc_monitor.py` | Python script for PC-side data collection |
| `build_upload.bat` | Build and upload script |
| `install_deps.bat` | Python dependency installer |

## Data Protocol

5-byte binary format over BLE:
- Byte 0: CPU usage (%)
- Byte 1: GPU usage (%)
- Byte 2: Memory usage (%)
- Byte 3: GPU temperature (°C)
- Byte 4: CPU temperature (°C)
