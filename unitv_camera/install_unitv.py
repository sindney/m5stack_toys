#!/usr/bin/env python3
"""
UnitV 固件安装脚本
将 boot.py 写入 UnitV 的 flash，使其开机自动运行摄像头流

用法: python install_unitv.py [COM端口]
默认: COM9
"""

import serial
import time
import sys

# 默认串口
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM9"
BAUD = 115200

# boot.py 内容（带握手协议的摄像头流）
BOOT_CODE = '''
import sensor
import image
import time
from fpioa_manager import fm
from machine import UART

# LED 控制
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

set_led(50, 0, 0)  # 红色: 等待

# UART 初始化 (Grove 接口)
fm.register(34, fm.fpioa.UART1_TX, force=True)
fm.register(35, fm.fpioa.UART1_RX, force=True)
uart = UART(UART.UART1, 115200, 8, 0, 1, timeout=1000, read_buf_len=4096)

# 摄像头初始化
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QQVGA)  # 160x120
sensor.set_vflip(False)
sensor.set_hmirror(False)
sensor.run(1)
time.sleep_ms(300)

# 握手阶段
handshake_done = False
blink = True
cnt = 0

while not handshake_done:
    if blink:
        set_led(50, 30, 0)  # 黄色
    else:
        set_led(10, 5, 0)   # 暗黄
    blink = not blink
    
    if uart.any():
        data = uart.read()
        if data:
            msg = data.decode("utf-8", "ignore")
            if "M5_READY" in msg:
                set_led(0, 0, 50)  # 蓝色
                uart.write(b"UNITV_OK\\n")
            elif "START" in msg:
                handshake_done = True
    
    cnt += 1
    if cnt % 20 == 0:
        uart.write(b"UNITV_OK\\n")
    
    time.sleep_ms(100)

# 图像传输阶段
set_led(0, 50, 0)  # 绿色
fc = 0
led_on = True

while True:
    img = sensor.snapshot()
    jpg = img.compress(quality=30).to_bytes()
    ln = len(jpg)
    
    # 发送长度头 (4字节大端)
    hdr = bytes([(ln>>24)&0xFF, (ln>>16)&0xFF, (ln>>8)&0xFF, ln&0xFF])
    uart.write(hdr)
    time.sleep_ms(5)
    
    # 分块发送
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
    
    # 检查停止命令
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

def main():
    print("=" * 50)
    print("  UnitV 固件安装脚本")
    print("=" * 50)
    print(f"串口: {PORT}")
    print()

    try:
        ser = serial.Serial(PORT, BAUD, timeout=2)
        print(f"[OK] Connected {PORT}")
        
        # Stop current program with Ctrl-C
        print("  Stopping current program...")
        ser.write(b'\x03')
        time.sleep(1)
        ser.write(b'\x03')
        time.sleep(1)
        # Drain buffer
        if ser.in_waiting:
            ser.read(ser.in_waiting)
        
        # Enter REPL - send a few newlines
        ser.write(b'\r\n')
        time.sleep(0.5)
        ser.write(b'\r\n')
        time.sleep(0.5)
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            print(f"  REPL: {repr(data[-80:])}")
        
        print("  Writing boot.py...")
        
        # Open file
        ser.write(b'f = open("boot.py", "w")\r\n')
        time.sleep(0.3)
        if ser.in_waiting:
            ser.read(ser.in_waiting)
        
        # Write lines one by one with delay
        lines = BOOT_CODE.strip().split('\n')
        for i, line in enumerate(lines):
            escaped = line.replace('\\', '\\\\').replace('"', '\\"')
            cmd = f'f.write("{escaped}\\n")\r\n'
            ser.write(cmd.encode('utf-8'))
            time.sleep(0.05)  # 50ms per line
            # Drain echo periodically
            if ser.in_waiting > 256:
                ser.read(ser.in_waiting)
        
        # Close file
        ser.write(b'f.close()\r\n')
        time.sleep(0.5)
        if ser.in_waiting:
            ser.read(ser.in_waiting)
        
        # Verify
        ser.write(b'import os; print(os.stat("boot.py"))\r\n')
        time.sleep(0.5)
        ser.write(b'print("DONE")\r\n')
        time.sleep(0.5)
        
        response = ""
        if ser.in_waiting:
            response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        
        print(f"  Response: {response[-200:]}")
        
        if "DONE" in response or ">>>" in response or "stat" in response.lower():
            print("[OK] boot.py install success!")
            print()
            print("  Starting camera stream...")
            ser.write(b'exec(open("boot.py").read())\r\n')
            time.sleep(1)
            
            print()
            print("=" * 50)
            print("  UnitV LED status:")
            print("    Red      = Waiting for M5Stack")
            print("    Yellow   = Handshake")
            print("    Green    = Streaming")
            print("=" * 50)
        else:
            print("[FAIL] Install failed")
            print(response)
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"[ERR] Serial error: {e}")
        print(f"  Check UnitV is on {PORT}")
    except Exception as e:
        print(f"[ERR] Error: {e}")

if __name__ == "__main__":
    main()
