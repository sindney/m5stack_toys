# UnitV Camera Stream - Boot Script
# 开机自动运行，等待 M5Stack 握手后开始传输

import sensor
import image
import time
from fpioa_manager import fm
from machine import UART

print("=== UnitV Camera Stream ===")
print("Waiting for M5Stack handshake...")

# ============ LED 初始化 ============
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

# 红色: 等待握手
set_led(50, 0, 0)

# ============ UART 初始化 ============
fm.register(34, fm.fpioa.UART1_TX, force=True)
fm.register(35, fm.fpioa.UART1_RX, force=True)
uart = UART(UART.UART1, 57600, 8, 0, 1, timeout=1000, read_buf_len=4096)

# ============ 摄像头初始化 ============
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QQVGA)  # 160x120
sensor.set_vflip(False)
sensor.set_hmirror(False)
sensor.run(1)
time.sleep_ms(300)
print("Camera: Ready")

# ============ 握手阶段 ============
# 黄色闪烁: 等待握手
handshake_done = False
blink_state = True
wait_count = 0

rx_buffer = b""  # 累积接收缓冲

while not handshake_done:
    # LED 闪烁表示等待中
    if blink_state:
        set_led(50, 30, 0)  # 黄色
    else:
        set_led(10, 5, 0)   # 暗黄
    blink_state = not blink_state
    
    # 检查是否收到 M5Stack 的握手信号
    if uart.any():
        data = uart.read()
        if data:
            rx_buffer += data
            # 保持缓冲区不太大
            if len(rx_buffer) > 256:
                rx_buffer = rx_buffer[-256:]
            
            # 尝试解码
            try:
                msg = rx_buffer.decode('utf-8', 'ignore')
            except:
                msg = ""
            
            # 打印调试信息
            print("RX:", repr(data[:20]))
            
            # 检查 START 命令 (优先级最高)
            # 更宽松的检测：只要看到 START 字样就开始
            if b"START" in rx_buffer or b"START" in data or "START" in msg:
                print(">>> Detected START! Beginning stream...")
                handshake_done = True
                rx_buffer = b""
                break
            
            # 也检测部分匹配 (如 TART, ART 等，可能因噪声丢失了 S)
            if b"TART" in data or b"ART\n" in data:
                print(">>> Detected partial START! Beginning stream...")
                handshake_done = True
                rx_buffer = b""
                break
            
            # 检查 M5_READY 命令
            if b"M5_READY" in rx_buffer or b"M5_READY" in data or b"READY" in data:
                print("M5 ready, sending UNITV_OK...")
                set_led(0, 0, 50)  # 蓝色: 握手中
                uart.write(b"UNITV_OK\n")
                time.sleep_ms(30)
                uart.write(b"UNITV_OK\n")  # 发两次确保收到
                rx_buffer = b""
    
    wait_count += 1
    if wait_count % 30 == 0:
        # 每 1.5 秒也主动发一次 UNITV_OK（防止丢包）
        uart.write(b"UNITV_OK\n")
        print("Sent UNITV_OK (periodic)")
    
    time.sleep_ms(50)  # 减少延迟以更快响应

# ============ 图像传输阶段 ============
# 绿色: 传输中
set_led(0, 50, 0)
print("=== Streaming Started ===")

# 等待一下让接收方准备好
time.sleep_ms(500)

# 清空接收缓冲
while uart.any():
    uart.read()

frame_count = 0
led_on = True

while True:
    # 捕获并压缩
    img = sensor.snapshot()
    jpg = img.compress(quality=30).to_bytes()
    ln = len(jpg)
    
    # 发送长度头 (4字节大端)
    hdr = bytes([(ln>>24)&0xFF, (ln>>16)&0xFF, (ln>>8)&0xFF, ln&0xFF])
    uart.write(hdr)
    time.sleep_ms(5)
    
    # 分块发送 JPEG
    sent = 0
    while sent < ln:
        chunk = jpg[sent:sent+256]
        uart.write(chunk)
        sent += len(chunk)
        time.sleep_ms(3)
    
    frame_count += 1
    
    # LED 闪烁指示活动
    if frame_count % 5 == 0:
        led_on = not led_on
        set_led(0, 50 if led_on else 10, 0)
    
    if frame_count % 30 == 0:
        print("Frame", frame_count, "size", ln)
    
    # 检查是否收到停止命令
    if uart.any():
        data = uart.read()
        if data and b"STOP" in data:
            print("Received STOP command")
            set_led(50, 0, 0)  # 红色
            # 重新进入握手等待
            handshake_done = False
            while not handshake_done:
                if uart.any():
                    d = uart.read()
                    if d and b"START" in d:
                        handshake_done = True
                        set_led(0, 50, 0)
                time.sleep_ms(100)
    
    time.sleep_ms(50)
