"""Test CamS3 5MP firmware via USB Serial (CDC).
Sends PING and CAP commands to verify the firmware is working.
Usage: python test_cams3.py [COM_PORT]
"""
import sys
import serial
import serial.tools.list_ports
import time

def scan_ports():
    """List all available COM ports."""
    ports = serial.tools.list_ports.comports()
    print(f"Found {len(ports)} port(s):")
    for p in ports:
        print(f"  {p.device}: {p.description} [hwid={p.hwid}]")
    return ports

def try_port(port, baud=115200):
    """Try to open a port and test PING/PONG."""
    print(f"\n{'='*50}")
    print(f"Testing {port} @ {baud}...")
    print(f"{'='*50}")
    
    try:
        ser = serial.Serial(port, baud, timeout=2)
    except serial.SerialException as e:
        print(f"  Cannot open: {e}")
        return False
    
    time.sleep(1.5)  # Wait for device to settle after open
    
    # Drain any boot messages
    boot_msgs = []
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            boot_msgs.append(line)
            print(f"  [boot] {line}")
    
    # Test 1: PING
    print("\n--- Test PING ---")
    ser.write(b"PING\n")
    time.sleep(0.5)
    response = ""
    while ser.in_waiting:
        response += ser.read(ser.in_waiting).decode('utf-8', errors='replace')
    response = response.strip()
    print(f"  Response: '{response}'")
    
    if "PONG" in response:
        print("  >>> PING/PONG OK!")
    else:
        print("  >>> No PONG response")
        ser.close()
        return False
    
    # Test 2: CAP (capture image)
    print("\n--- Test CAP ---")
    ser.write(b"CAP\n")
    time.sleep(3)  # Give time for capture + encode + send
    avail = ser.in_waiting
    print(f"  Bytes available: {avail}")
    
    if avail > 0:
        # Read header line
        header = ser.readline().decode('utf-8', errors='replace').strip()
        print(f"  Header: '{header}'")
        
        if header.startswith("IMG:"):
            img_len = int(header[4:])
            print(f"  Expected JPEG size: {img_len} bytes")
            
            # Read JPEG data
            data = b""
            remaining = img_len
            start = time.time()
            while remaining > 0 and (time.time() - start) < 10:
                chunk = ser.read(min(remaining, 4096))
                if chunk:
                    data += chunk
                    remaining -= len(chunk)
            
            print(f"  Received: {len(data)} bytes")
            if len(data) == img_len:
                print("  >>> CAP/IMG OK!")
                # Check JPEG markers
                if data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9':
                    print("  >>> Valid JPEG (FFD8...FFD9)")
                    # Save test image
                    out_path = "test_capture.jpg"
                    with open(out_path, "wb") as f:
                        f.write(data)
                    print(f"  >>> Saved to {out_path}")
                else:
                    print(f"  !!! Invalid JPEG markers: start={data[:2].hex()} end={data[-2:].hex()}")
            else:
                print(f"  !!! Size mismatch: expected {img_len}, got {len(data)}")
        elif header.startswith("LOG:"):
            print(f"  Log message: {header}")
            # Try reading more
            time.sleep(1)
            while ser.in_waiting:
                extra = ser.readline().decode('utf-8', errors='replace').strip()
                print(f"  Extra: '{extra}'")
        elif header.startswith("ERR:"):
            print(f"  Camera error: {header}")
        else:
            print(f"  Unexpected response: {header}")
    else:
        print("  No response to CAP")
    
    ser.close()
    return True

# --- Main ---
print("=== CamS3 5MP Firmware Test ===\n")

ports = scan_ports()

if len(sys.argv) > 1:
    # User specified a port
    try_port(sys.argv[1])
else:
    # Try all USB-related ports
    usb_ports = [p.device for p in ports if "USB" in p.description.upper()]
    if not usb_ports:
        # Fallback: try all non-COM1 ports
        usb_ports = [p.device for p in ports if p.device != "COM1"]
    
    if not usb_ports:
        print("\nNo USB serial ports found!")
        print("Make sure CamS3 5MP is connected via USB.")
    else:
        print(f"\nTesting USB ports: {usb_ports}")
        for port in usb_ports:
            try_port(port)

print("\nDone.")
