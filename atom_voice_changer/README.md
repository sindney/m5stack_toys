# Atom Voice Changer

A real-time voice changer with cyberpunk-style UI for M5Stack AtomS3R with Atomic Echo Base.

## Features

- **6 Voice Effects**: Deep Bass, Chipmunk, Robot, Echo, Alien, and Whisper
- **Auto Voice Detection**: Automatically detects voice activity and starts recording
- **Tap to Cycle**: Simple tap interaction to switch between effects
- **Cyberpunk UI**: Stylish color palette with animated waveform display
- **Real-time Processing**: Low-latency audio transformation

## Hardware Requirements

- **M5Stack AtomS3R** (ESP32-S3 based)
- **Atomic Echo Base** with:
  - ES8311 audio codec
  - MEMS microphone
  - NS4150B speaker amplifier

## Usage

1. Power on the device
2. Tap the screen to cycle through voice effects (OFF → FX1 → FX2 → ... → FX6 → OFF)
3. In FX mode, the device automatically listens, records when voice is detected, transforms, and plays back

## Build & Upload

Use Arduino CLI or Arduino IDE with ESP32-S3 board support:

```batch
build_upload.bat
```

## Files

| File | Description |
|------|-------------|
| `atom_voice_changer.ino` | Main Arduino source code |
| `build_upload.bat` | Build and upload script |
| `read_serial.ps1` | PowerShell script for serial monitoring |

## License

MIT License - 2026
