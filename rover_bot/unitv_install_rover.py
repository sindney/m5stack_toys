#!/usr/bin/env python3
"""
UnitV 固件安装脚本 — rover_bot 版本
参考 unitv_camera/install_unitv.py 的成功方法

用法: python unitv_install_rover.py [COM端口]
默认: COM5
"""

import serial
import time
import sys
import os

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = 115200

# 从文件读取 boot.py 内容
BOOT_PY_PATH = os.path.join(os.path.dirname(__file__), "unitv_firmware", "boot.py")
with open(BOOT_PY_PATH, "r", encoding="utf-8") as f:
    BOOT_CODE = f.read()

print("=" * 55)
print("  UnitV 固件安装 (rover_bot)")
print("=" * 55)
print(f"  串口: {PORT}")
print(f"  boot.py: {len(BOOT_CODE)} bytes, {len(BOOT_CODE.splitlines())} lines")
print()


def send_cmd(ser, cmd, delay=0.1):
    """发送命令并等待"""
    ser.write((cmd + "\r\n").encode("utf-8"))
    time.sleep(delay)


def read_all(ser):
    """读取所有可用数据"""
    time.sleep(0.1)
    data = b""
    while ser.in_waiting:
        data += ser.read(ser.in_waiting)
        time.sleep(0.05)
    return data.decode("utf-8", errors="ignore")


def interrupt_and_get_repl(ser):
    """中断当前程序，获取 REPL 提示符"""
    print("  [1/5] 中断当前程序...")
    
    # 多次 Ctrl+C 确保中断
    for i in range(5):
        ser.write(b"\x03")
        time.sleep(0.3)
    
    # 清空缓冲
    ser.read(ser.in_waiting or 1)
    time.sleep(0.5)
    
    # 发送回车，检查是否有 >>> 提示符
    ser.write(b"\r\n")
    time.sleep(0.3)
    resp = read_all(ser)
    
    if ">>>" in resp:
        print("  [OK] 进入 REPL")
        return True
    
    # 再试一次
    for i in range(3):
        ser.write(b"\x03")
        time.sleep(0.5)
    ser.write(b"\r\n")
    time.sleep(0.5)
    resp = read_all(ser)
    
    if ">>>" in resp:
        print("  [OK] 进入 REPL")
        return True
    
    print(f"  [WARN] 未检测到 >>> 提示符，继续尝试...")
    print(f"  收到: {repr(resp[:200])}")
    return True  # 继续尝试


def write_boot_py(ser):
    """用你之前验证过的方法 — 逐行写入 boot.py"""
    print("  [2/5] 写入 boot.py...")
    
    # 清空缓冲
    read_all(ser)
    
    # 打开文件
    send_cmd(ser, 'f = open("boot.py", "w")', delay=0.2)
    resp = read_all(ser)
    if "Error" in resp or "Traceback" in resp:
        print(f"  [ERROR] 无法创建文件: {resp}")
        return False
    
    # 逐行写入
    lines = BOOT_CODE.strip().split('\n')
    total = len(lines)
    for i, line in enumerate(lines):
        # 转义反斜杠和双引号
        escaped = line.replace('\\', '\\\\').replace('"', '\\"')
        cmd = f'f.write("{escaped}\\n")\r\n'
        ser.write(cmd.encode('utf-8'))
        time.sleep(0.02)  # 和你之前脚本一样的延迟
        
        # 每 20 行读一次缓冲防止溢出
        if (i + 1) % 20 == 0:
            ser.read(ser.in_waiting or 1)
            print(f"    进度: {i+1}/{total} 行")
    
    # 关闭文件
    time.sleep(0.2)
    ser.read(ser.in_waiting or 1)
    send_cmd(ser, 'f.close()', delay=0.3)
    resp = read_all(ser)
    
    print(f"  [OK] 写入 {total} 行完成")
    return True


def verify_boot_py(ser):
    """验证写入是否成功"""
    print("  [3/5] 验证文件...")
    
    read_all(ser)
    
    # 检查文件大小
    send_cmd(ser, 'import os; print("SIZE:", os.stat("boot.py")[6])', delay=0.5)
    resp = read_all(ser)
    
    if "SIZE:" in resp:
        for line in resp.split("\n"):
            if "SIZE:" in line:
                print(f"  [OK] {line.strip()}")
                break
    else:
        print(f"  [WARN] 无法读取文件大小: {resp[:100]}")
    
    # 检查前几行
    send_cmd(ser, 'f=open("boot.py"); print("LINE1:", f.readline().strip()); f.close()', delay=0.5)
    resp = read_all(ser)
    if "LINE1:" in resp:
        for line in resp.split("\n"):
            if "LINE1:" in line:
                print(f"  [OK] {line.strip()}")
                break
    
    return True


def test_exec(ser):
    """用 exec 执行 boot.py — 和你 unitv_camera 项目一样的方法"""
    print("  [4/5] 执行 boot.py...")
    
    read_all(ser)
    send_cmd(ser, 'exec(open("boot.py").read())', delay=2.0)
    resp = read_all(ser)
    
    print(f"  启动输出: {resp[:300]}")
    return True


def test_ping(ser):
    """测试 PING/PONG"""
    print("  [5/5] 测试 PING...")
    
    time.sleep(1)  # 等待 boot.py 初始化完成
    read_all(ser)  # 清空
    
    ser.write(b"PING\n")
    time.sleep(0.5)
    resp = read_all(ser)
    
    if "PONG" in resp:
        print("  [OK] PING -> PONG 成功!")
        return True
    else:
        print(f"  [INFO] PING 响应: {repr(resp[:200])}")
        # 再试一次
        time.sleep(1)
        read_all(ser)
        ser.write(b"PING\n")
        time.sleep(1)
        resp = read_all(ser)
        if "PONG" in resp:
            print("  [OK] PING -> PONG 成功 (第二次)")
            return True
        print(f"  [INFO] 第二次 PING 响应: {repr(resp[:200])}")
        return False


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=2)
        print(f"  已连接 {PORT}")
        print()
        
        # Step 1: 中断出厂脚本，进入 REPL
        interrupt_and_get_repl(ser)
        
        # Step 2: 写入 boot.py
        if not write_boot_py(ser):
            print("\n  写入失败!")
            ser.close()
            return
        
        # Step 3: 验证
        verify_boot_py(ser)
        
        # Step 4: 执行
        test_exec(ser)
        
        # Step 5: 测试 PING
        ping_ok = test_ping(ser)
        
        print()
        print("=" * 55)
        if ping_ok:
            print("  安装成功! UnitV 已运行 rover_bot 固件")
            print()
            print("  注意: exec() 方式只在本次上电有效")
            print("  要永久生效，需要刷干净的 MaixPy 固件")
            print("  (出厂 _boot.py 会在重启时抢占 boot.py)")
        else:
            print("  boot.py 已写入，但 PING 测试未通过")
            print("  可能需要等更长时间让摄像头初始化")
            print()
            print("  你可以手动测试:")
            print("  1. 用串口工具连接 COM5 (115200)")
            print("  2. 按 Ctrl+C 中断")
            print("  3. 输入: exec(open('boot.py').read())")
            print("  4. 等几秒后输入: PING")
        print("=" * 55)
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"\n  串口错误: {e}")
        print(f"  请确认 UnitV 连接到 {PORT}")
    except Exception as e:
        print(f"\n  错误: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
