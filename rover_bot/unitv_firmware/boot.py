# =============================================================================
#  UnitV OV2640 — MaixPy Firmware for XiaoChong Rover
# =============================================================================
#  This script runs on the UnitV (Kendryte K210) as boot.py.
#  It listens for commands on UART and sends back image data / detection results.
#
#  Protocol:
#    RX commands (from StickC Plus):
#      "PING\n"  → responds with "PONG\n"
#      "CAP\n"   → capture JPEG image, send "IMG:<length>\n" + raw JPEG bytes
#      "DET\n"   → run basic obstacle detection, send "OBS:<x>,<y>,<w>,<h>,<conf>\n"
#
#    TX responses (to StickC Plus):
#      "PONG\n"              → alive acknowledgment
#      "IMG:<length>\n<data>" → JPEG image data
#      "OBS:<results>\n"     → obstacle detection results
#      "LOG:<message>\n"     → debug log messages
#
#  Hardware:
#    - Kendryte K210 dual-core RISC-V @ 400MHz
#    - OV2640 camera sensor (detected by MaixPy)
#    - UART via Grove connector (pins 34/35)
#
#  Flashing:
#    Use Kflash_GUI to flash M5StickV firmware first,
#    then copy this file to /flash/boot.py via MaixPy IDE or serial REPL.
# =============================================================================

import sensor
import image
import time
import gc
from machine import UART
from fpioa_manager import fm
# =============================================================================
#  WS2812 RGB LED Setup (GPIO 8, 1 LED on UnitV board)
#  Note: ws2812 module is NOT available in minimum firmware,
#        use non-minimum (standard) firmware for LED support.
# =============================================================================
try:
    from modules import ws2812
    led = ws2812(8, 1)
    HAS_LED = True
except:
    HAS_LED = False

connected = False       # True after first PING received
led_toggle = False      # For red blink animation

# =============================================================================
#  Pin Configuration
# =============================================================================
# UnitV Grove connector: pin 34 (TX out), pin 35 (RX in)
# These map to the StickC Plus Grove: G32 (RX), G33 (TX)
fm.register(34, fm.fpioa.UART1_TX, force=True)
fm.register(35, fm.fpioa.UART1_RX, force=True)

# =============================================================================
#  UART Setup
# =============================================================================
uart = UART(UART.UART1, baudrate=115200, bits=8, parity=None, stop=1,
            timeout=100, read_buf_len=256)

# =============================================================================
#  Camera Setup
# =============================================================================
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)       # 320x240
sensor.set_hmirror(False)
sensor.set_vflip(False)
sensor.set_auto_gain(True)
sensor.set_auto_whitebal(True)
sensor.skip_frames(time=2000)            # Let auto-exposure stabilize

# JPEG quality (lower = better quality, larger file)
JPEG_QUALITY = 50  # Balance between quality and UART transfer speed

# =============================================================================
#  Helper Functions
# =============================================================================

def send_line(msg):
    """Send a text line over UART."""
    uart.write(msg + "\n")

def send_log(msg):
    """Send a debug log message."""
    send_line("LOG:" + msg)

def set_led(r, g, b):
    """Set the onboard WS2812 LED color."""
    if HAS_LED:
        led.set_led(0, (r, g, b))
        led.display()

def led_off():
    """Turn off the LED."""
    set_led(0, 0, 0)

def capture_and_send():
    """Capture a JPEG image and send it over UART."""
    gc.collect()
    img = sensor.snapshot()

    # Compress to JPEG
    jpg = img.compress(quality=JPEG_QUALITY)
    jpg_bytes = jpg.to_bytes()
    length = len(jpg_bytes)

    # Send header
    send_line("IMG:" + str(length))
    time.sleep_ms(5)  # Small delay for receiver to prepare

    # Send raw JPEG data in chunks (UART buffer is limited)
    chunk_size = 1024
    offset = 0
    while offset < length:
        end = min(offset + chunk_size, length)
        uart.write(jpg_bytes[offset:end])
        offset = end
        time.sleep_ms(2)  # Prevent UART overflow

    del jpg_bytes
    del jpg
    gc.collect()

def detect_obstacles():
    """Basic obstacle detection using color/edge analysis.

    Returns simple obstacle info based on dark regions in the lower half
    of the image (likely close obstacles).

    For more advanced detection, load a KPU model (YOLO etc).
    """
    img = sensor.snapshot()

    # Simple approach: find dark blobs in the center-bottom region
    # (obstacles tend to be darker / occlude the floor)
    roi = (40, 120, 240, 120)  # Bottom half, center region
    gray = img.to_grayscale()

    # Find dark blobs (potential obstacles)
    blobs = gray.find_blobs([(0, 80)],  # Dark threshold
                            roi=roi,
                            pixels_threshold=500,
                            area_threshold=500,
                            merge=True)

    if blobs:
        # Report the largest blob
        largest = max(blobs, key=lambda b: b.area())
        result = "{},{},{},{},{}".format(
            largest.cx(), largest.cy(),
            largest.w(), largest.h(),
            largest.area()
        )
        send_line("OBS:" + result)
    else:
        send_line("OBS:CLEAR")

    del gray
    gc.collect()

# =============================================================================
#  Main Loop
# =============================================================================

send_log("UnitV booting...")
set_led(50, 0, 0)  # Red on boot: waiting for connection
send_line("PONG")  # Signal that we're alive
send_log("Ready. Waiting for commands.")

cmd_buf = ""
clock = time.clock()
blink_counter = 0

while True:
    clock.tick()

    # --- LED status indicator ---
    if connected:
        # Solid green when connected
        set_led(0, 20, 0)
    else:
        # Blink red when waiting for connection (~2Hz at 10ms loop)
        blink_counter += 1
        if blink_counter >= 25:
            blink_counter = 0
            led_toggle = not led_toggle
        if led_toggle:
            set_led(20, 0, 0)
        else:
            led_off()

    # Read UART commands
    if uart.any():
        data = uart.read()
        if data:
            try:
                cmd_buf += data.decode('utf-8', 'ignore')
            except:
                cmd_buf = ""
                continue

            # Process complete lines
            while "\n" in cmd_buf:
                line, cmd_buf = cmd_buf.split("\n", 1)
                line = line.strip()

                if line == "PING":
                    if not connected:
                        connected = True
                        send_log("Connected!")
                    send_line("PONG")

                elif line == "CAP":
                    capture_and_send()

                elif line == "DET":
                    detect_obstacles()

                elif line == "FPS":
                    send_line("FPS:" + str(int(clock.fps())))

                elif line.startswith("QUAL:"):
                    # Set JPEG quality: QUAL:50
                    try:
                        JPEG_QUALITY = int(line[5:])
                        JPEG_QUALITY = max(10, min(95, JPEG_QUALITY))
                        send_log("Quality set to " + str(JPEG_QUALITY))
                    except:
                        pass

    # Keep camera running (for auto-exposure)
    sensor.snapshot()
    time.sleep_ms(10)
