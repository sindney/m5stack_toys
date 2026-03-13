#!/usr/bin/env python3
"""
M5Stocks 配置工具 - 通过串口配置 M5Stack 上的 WiFi 和股票列表

依赖安装:
    pip install pyserial

使用方法:
    python config_stocks.py              # 交互式配置
    python config_stocks.py --read       # 读取当前配置
    python config_stocks.py --port COM8  # 指定串口
"""

import sys
import time
import argparse
import glob

# 检查依赖
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("错误: 缺少 pyserial 库")
    print("      pip install pyserial")
    sys.exit(1)


def find_m5stack_port() -> str:
    """自动检测 M5Stack 串口"""
    ports = list(serial.tools.list_ports.comports())
    
    if not ports:
        print("未检测到任何串口设备")
        return None
    
    # 优先匹配常见的 M5Stack USB 芯片
    m5_keywords = ['CP210', 'CH340', 'CH9102', 'FTDI', 'Silicon Labs', 'USB Serial']
    
    candidates = []
    for port in ports:
        desc = f"{port.description} {port.manufacturer or ''}"
        for kw in m5_keywords:
            if kw.lower() in desc.lower():
                candidates.append(port)
                break
    
    if len(candidates) == 1:
        print(f"自动检测到设备: {candidates[0].device} ({candidates[0].description})")
        return candidates[0].device
    
    # 多个或没有匹配，让用户选择
    print("\n检测到的串口设备:")
    print("-" * 60)
    for i, port in enumerate(ports, 1):
        print(f"  [{i}] {port.device} - {port.description}")
    print("-" * 60)
    
    while True:
        try:
            choice = input(f"请选择设备 [1-{len(ports)}]: ").strip()
            idx = int(choice) - 1
            if 0 <= idx < len(ports):
                return ports[idx].device
        except (ValueError, KeyboardInterrupt):
            return None


def send_command(ser: serial.Serial, cmd: str, wait: float = 0.5) -> list:
    """发送命令并读取响应"""
    # 清空缓冲
    ser.reset_input_buffer()
    
    # 发送
    full_cmd = cmd + '\n'
    ser.write(full_cmd.encode('utf-8'))
    ser.flush()
    
    # 等待响应
    time.sleep(wait)
    
    lines = []
    while ser.in_waiting > 0:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                lines.append(line)
        except Exception:
            break
    
    return lines


def read_config(ser: serial.Serial):
    """读取当前配置"""
    print("\n读取设备配置...")
    responses = send_command(ser, "CFG:LIST", 1.0)
    
    if not responses:
        print("  未收到响应（设备可能不在配置监听状态）")
        return
    
    wifi_ssid = ""
    stocks = []
    
    for line in responses:
        if line.startswith("WIFI:"):
            wifi_ssid = line[5:]
        elif line.startswith("STOCKS:"):
            pass
        elif line.startswith("STOCK:"):
            parts = line.split(":", 3)
            if len(parts) >= 4:
                stocks.append({"idx": parts[1], "code": parts[2], "name": parts[3]})
        elif line == "END":
            break
    
    print("\n" + "=" * 50)
    print("  当前设备配置")
    print("=" * 50)
    print(f"  WiFi SSID : {wifi_ssid if wifi_ssid else '(未配置)'}")
    print(f"  股票数量  : {len(stocks)}")
    if stocks:
        print("  ---")
        for s in stocks:
            print(f"  [{s['idx']}] {s['code']} - {s['name']}")
    print("=" * 50)


def interactive_config(ser: serial.Serial):
    """交互式配置"""
    print("\n" + "=" * 50)
    print("  M5Stocks 交互式配置")
    print("=" * 50)
    
    # 先读取当前配置
    read_config(ser)
    
    print("\n操作选项:")
    print("  [1] 配置 WiFi")
    print("  [2] 添加股票")
    print("  [3] 删除股票")
    print("  [4] 清空所有股票")
    print("  [5] 查看当前配置")
    print("  [6] 保存并重启")
    print("  [0] 退出")
    
    while True:
        try:
            choice = input("\n请选择操作 > ").strip()
            
            if choice == '0':
                print("退出配置。")
                break
                
            elif choice == '1':
                ssid = input("  WiFi SSID: ").strip()
                password = input("  WiFi 密码: ").strip()
                if ssid:
                    # 处理包含空格的 SSID
                    cmd = f'CFG:WIFI "{ssid}" "{password}"'
                    resp = send_command(ser, cmd)
                    for r in resp:
                        print(f"  <- {r}")
                else:
                    print("  SSID 不能为空")
                    
            elif choice == '2':
                code = input("  股票代码 (如 00700): ").strip()
                name = input("  股票名称 (如 Tencent): ").strip()
                if not name:
                    name = code
                if code:
                    resp = send_command(ser, f"CFG:ADD {code} {name}")
                    for r in resp:
                        print(f"  <- {r}")
                else:
                    print("  代码不能为空")
                    
            elif choice == '3':
                code = input("  要删除的股票代码: ").strip()
                if code:
                    resp = send_command(ser, f"CFG:DEL {code}")
                    for r in resp:
                        print(f"  <- {r}")
                        
            elif choice == '4':
                confirm = input("  确认清空所有股票? (y/N): ").strip().lower()
                if confirm == 'y':
                    resp = send_command(ser, "CFG:CLEAR")
                    for r in resp:
                        print(f"  <- {r}")
                        
            elif choice == '5':
                read_config(ser)
                
            elif choice == '6':
                print("  保存配置...")
                resp = send_command(ser, "CFG:SAVE", 1.0)
                for r in resp:
                    print(f"  <- {r}")
                
                confirm = input("  是否立即重启设备? (Y/n): ").strip().lower()
                if confirm != 'n':
                    print("  重启设备...")
                    send_command(ser, "CFG:REBOOT", 0.5)
                    print("  设备正在重启，请稍候...")
                    break
                    
            else:
                print("  无效选项")
                
        except KeyboardInterrupt:
            print("\n\n已取消。")
            break


def quick_setup(ser: serial.Serial, ssid: str, password: str, stocks_list: list):
    """快速配置（非交互式）"""
    print("\n快速配置模式...")
    
    # 配置 WiFi
    if ssid:
        resp = send_command(ser, f'CFG:WIFI "{ssid}" "{password}"')
        print(f"WiFi: {' '.join(resp)}")
    
    # 清空旧股票
    send_command(ser, "CFG:CLEAR")
    
    # 添加股票
    for stock in stocks_list:
        parts = stock.split(':')
        code = parts[0]
        name = parts[1] if len(parts) > 1 else code
        resp = send_command(ser, f"CFG:ADD {code} {name}")
        print(f"添加 {code}: {' '.join(resp)}")
    
    # 保存
    resp = send_command(ser, "CFG:SAVE", 1.0)
    print(f"保存: {' '.join(resp)}")
    
    # 重启
    print("重启设备...")
    send_command(ser, "CFG:REBOOT", 0.5)
    print("完成！")


def main():
    parser = argparse.ArgumentParser(description='M5Stocks 配置工具')
    parser.add_argument('--port', type=str, help='串口端口 (如 COM8)')
    parser.add_argument('--baud', type=int, default=115200, help='波特率 (默认 115200)')
    parser.add_argument('--read', action='store_true', help='读取当前配置')
    parser.add_argument('--wifi', type=str, help='快速配置 WiFi (格式: SSID:PASSWORD)')
    parser.add_argument('--add', type=str, nargs='+', help='快速添加股票 (格式: CODE:NAME)')
    args = parser.parse_args()
    
    print("=" * 50)
    print("  M5Stocks 配置工具 v1.0")
    print("=" * 50)
    
    # 确定串口
    port = args.port
    if not port:
        port = find_m5stack_port()
        if not port:
            print("\n错误: 无法确定串口。请使用 --port 参数指定。")
            sys.exit(1)
    
    # 打开串口
    try:
        ser = serial.Serial(port, args.baud, timeout=1)
        print(f"\n已连接: {port} @ {args.baud}bps")
        time.sleep(1)  # 等待设备就绪
        
        # 清空启动信息
        while ser.in_waiting:
            ser.readline()
        
    except serial.SerialException as e:
        print(f"\n错误: 无法打开串口 {port}: {e}")
        sys.exit(1)
    
    try:
        if args.read:
            read_config(ser)
        elif args.wifi or args.add:
            # 快速配置模式
            ssid, password = '', ''
            if args.wifi:
                parts = args.wifi.split(':', 1)
                ssid = parts[0]
                password = parts[1] if len(parts) > 1 else ''
            stocks_list = args.add or []
            quick_setup(ser, ssid, password, stocks_list)
        else:
            interactive_config(ser)
    finally:
        ser.close()
        print("\n串口已关闭。")


if __name__ == "__main__":
    main()
