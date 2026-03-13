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
        print(f"✓ 已连接 {PORT}")
        
        # 停止当前程序
        print("  停止当前程序...")
        ser.write(b'\x03')
        time.sleep(0.5)
        ser.write(b'\x03')
        time.sleep(0.5)
        ser.read(ser.in_waiting or 1)
        
        # 进入 REPL
        ser.write(b'\r\n')
        time.sleep(0.2)
        ser.read(ser.in_waiting or 1)
        
        print("  写入 boot.py...")
        
        # 写入文件
        ser.write(b'f = open("boot.py", "w")\r\n')
        time.sleep(0.1)
        
        # 逐行写入
        for line in BOOT_CODE.strip().split('\n'):
            escaped = line.replace('\\', '\\\\').replace('"', '\\"')
            cmd = f'f.write("{escaped}\\n")\r\n'
            ser.write(cmd.encode('utf-8'))
            time.sleep(0.02)
        
        ser.write(b'f.close()\r\n')
        time.sleep(0.2)
        
        ser.write(b'print("OK")\r\n')
        time.sleep(0.3)
        
        response = ser.read(ser.in_waiting or 1).decode('utf-8', errors='ignore')
        
        if "OK" in response or ">>>" in response:
            print("✓ boot.py 安装成功!")
            print()
            print("  启动摄像头流...")
            ser.write(b'exec(open("boot.py").read())\r\n')
            time.sleep(1)
            
            print()
            print("=" * 50)
            print("  UnitV LED 状态说明:")
            print("    红色     = 等待 M5Stack")
            print("    黄色闪烁 = 握手中")
            print("    绿色闪烁 = 传输中")
            print("=" * 50)
        else:
            print("✗ 安装失败")
            print(response)
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"✗ 串口错误: {e}")
        print(f"  请确认 UnitV 连接到 {PORT}")
    except Exception as e:
        print(f"✗ 错误: {e}")

if __name__ == "__main__":
    main()
