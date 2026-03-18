#!/usr/bin/env python3
"""
Install boot.py to UnitV flash

Usage:
    python install_boot_simple.py [PORT]

Examples:
    python install_boot_simple.py          # Uses default COM9
    python install_boot_simple.py COM5     # Uses COM5
    python install_boot_simple.py /dev/ttyUSB0  # Linux
"""

import serial
import time
import sys

# ==============================================
# Configuration
# ==============================================
# Default serial port (override with command line argument)
DEFAULT_PORT = "COM9"
BAUD = 115200

# Get port from command line or use default
PORT = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT

# boot.py content
BOOT_CODE = '''
import sensor
import image
import time
from fpioa_manager import fm
from machine import UART

# LED
try:
    from modules import ws2812
    led = ws2812(8, 1)
    HAS_LED = True
except:
    HAS_LED = False

def set_led(r, g, b):
    if HAS_LED:
        led.set_led(0, (r, g, b))
        led.display()

set_led(50, 0, 0)  # Red: waiting

# UART
fm.register(34, fm.fpioa.UART1_TX, force=True)
fm.register(35, fm.fpioa.UART1_RX, force=True)
uart = UART(UART.UART1, 115200, 8, 0, 1, timeout=1000, read_buf_len=4096)

# Camera
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QQVGA)
sensor.set_vflip(False)
sensor.set_hmirror(False)
sensor.run(1)
time.sleep_ms(300)

# Handshake
handshake_done = False
blink = True
cnt = 0

while not handshake_done:
    if blink:
        set_led(50, 30, 0)
    else:
        set_led(10, 5, 0)
    blink = not blink
    
    if uart.any():
        data = uart.read()
        if data:
            msg = data.decode("utf-8", "ignore")
            if "M5_READY" in msg:
                set_led(0, 0, 50)
                uart.write(b"UNITV_OK\\n")
            elif "START" in msg:
                handshake_done = True
    
    cnt += 1
    if cnt % 20 == 0:
        uart.write(b"UNITV_OK\\n")
    
    time.sleep_ms(100)

# Streaming
set_led(0, 50, 0)
fc = 0
led_on = True

while True:
    img = sensor.snapshot()
    jpg = img.compress(quality=30).to_bytes()
    ln = len(jpg)
    
    hdr = bytes([(ln>>24)&0xFF, (ln>>16)&0xFF, (ln>>8)&0xFF, ln&0xFF])
    uart.write(hdr)
    time.sleep_ms(5)
    
    sent = 0
    while sent < ln:
        chunk = jpg[sent:sent+256]
        uart.write(chunk)
        sent += len(chunk)
        time.sleep_ms(3)
    
    fc += 1
    if fc % 5 == 0:
        led_on = not led_on
        set_led(0, 50 if led_on else 10, 0)
    
    if uart.any():
        d = uart.read()
        if d and b"STOP" in d:
            set_led(50, 0, 0)
            handshake_done = False
            while not handshake_done:
                if uart.any():
                    dd = uart.read()
                    if dd and b"START" in dd:
                        handshake_done = True
                        set_led(0, 50, 0)
                time.sleep_ms(100)
    
    time.sleep_ms(50)
'''

print("=" * 50)
print("Installing boot.py to UnitV")
print("=" * 50)
print(f"Port: {PORT}")
print()

try:
    ser = serial.Serial(PORT, BAUD, timeout=2)
    print(f"Connected to {PORT}")
    
    # Stop current program
    print("Stopping current program...")
    ser.write(b'\x03')
    time.sleep(0.5)
    ser.write(b'\x03')
    time.sleep(0.5)
    ser.read(ser.in_waiting or 1)
    
    # Enter REPL
    ser.write(b'\r\n')
    time.sleep(0.2)
    ser.read(ser.in_waiting or 1)
    
    print("Writing boot.py...")
    
    # Write file using simple approach
    ser.write(b'f = open("boot.py", "w")\r\n')
    time.sleep(0.1)
    
    # Write content line by line
    for line in BOOT_CODE.strip().split('\n'):
        escaped = line.replace('\\', '\\\\').replace('"', '\\"')
        cmd = f'f.write("{escaped}\\n")\r\n'
        ser.write(cmd.encode('utf-8'))
        time.sleep(0.02)
    
    ser.write(b'f.close()\r\n')
    time.sleep(0.2)
    
    ser.write(b'print("SUCCESS")\r\n')
    time.sleep(0.3)
    
    response = ser.read(ser.in_waiting or 1).decode('utf-8', errors='ignore')
    print(response)
    
    if "SUCCESS" in response or ">>>" in response:
        print("\n" + "=" * 50)
        print("boot.py installed!")
        print("=" * 50)
        print("\nNow testing by running boot.py...")
        
        # Run boot.py
        ser.write(b'exec(open("boot.py").read())\r\n')
        time.sleep(1)
        
        print("\nUnitV should now show:")
        print("  - Red LED: Waiting for M5Stack")
        print("  - Yellow blinking: Handshaking") 
        print("  - Green: Streaming")
    
    ser.close()
    
except serial.SerialException as e:
    print(f"Error: {e}")
    print(f"Tip: Check if UnitV is connected to {PORT}")
    print(f"     You can specify a different port: python {sys.argv[0]} COM5")
except Exception as e:
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
