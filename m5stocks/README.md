# M5Stocks

A real-time Hong Kong stock market monitor for M5Stack Core.

## Features

- **Multi-Stock Support**: Monitor up to 10 stocks simultaneously
- **Serial Configuration**: Configure WiFi and stocks via Python script
- **Price History Chart**: Visual trend display with intraday history
- **Real-time Updates**: Automatic price refresh from market data
- **Cyberpunk Green UI**: Stylish green-themed interface
- **Persistent Storage**: Configuration saved to NVS Flash

## Hardware Requirements

- **M5Stack Core** (ESP32-based with 320x240 display and 3 buttons)
- **WiFi Network** with internet access

## Button Controls

| Button | Function |
|--------|----------|
| A (Left) | Previous stock |
| B (Middle) | Next stock |
| C (Right) | Force refresh data |

## Configuration

Use the Python configuration script to set up WiFi and stock list:

```bash
# Interactive configuration
python config_stocks.py

# Read current configuration
python config_stocks.py --read
```

### Configuration Commands

- `WIFI <ssid> <password>` - Set WiFi credentials
- `ADD <code> <name>` - Add a stock (e.g., `ADD 00700 Tencent`)
- `DEL <code>` - Remove a stock
- `CLEAR` - Remove all stocks
- `LIST` - Show current configuration
- `SAVE` - Save to flash
- `REBOOT` - Restart device

## Build & Upload

```batch
# Install Python dependencies
install_deps.bat

# Build and upload firmware
build_upload.bat
```

## Files

| File | Description |
|------|-------------|
| `m5stocks.ino` | Main Arduino firmware |
| `config_stocks.py` | Python configuration tool |
| `build_upload.bat` | Build and upload script |
| `install_deps.bat` | Python dependency installer |

## Display Layout

```
┌────────────────────────────────────┐
│ Stock Name              HH:MM:SS   │  Header
├────────────────────────────────────┤
│ $XXX.XX   +X.XX (+X.XX%)           │  Price Info
│ O: XXX  H: XXX  L: XXX             │
├────────────────────────────────────┤
│                                    │
│         Price Chart                │  Intraday Chart
│                                    │
├────────────────────────────────────┤
│ [◄ Prev]    [Next ►]    [Refresh]  │  Button Labels
└────────────────────────────────────┘
```
