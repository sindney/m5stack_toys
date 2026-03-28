#!/usr/bin/env python3
"""
小虫 (XiaoChong) Rover Bot — WiFi 配置工具

通过串口向 M5StickC Plus 发送 WiFi 配置信息。

依赖安装:
    pip install pyserial

使用方法:
    python wifi_config.py
    python wifi_config.py --port COM3
    python wifi_config.py --port COM3 --ssid MyWiFi --password 12345678
    python wifi_config.py --monitor    # 配置后进入串口监听模式
"""

import sys
import time
import argparse
import getpass

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("错误: 请先安装 pyserial")
    print("      pip install pyserial")
    sys.exit(1)

BAUD_RATE = 115200
TIMEOUT = 2


def list_com_ports():
    """列出所有可用的 COM 端口"""
    ports = serial.tools.list_ports.comports()
    return sorted(ports, key=lambda p: p.device)


def select_port(specified_port=None):
    """选择 COM 端口，支持自动检测"""
    if specified_port:
        return specified_port

    ports = list_com_ports()

    if not ports:
        print("❌ 未检测到任何 COM 端口！")
        print("   请确认 M5StickC Plus 已通过 USB 连接到电脑。")
        sys.exit(1)

    if len(ports) == 1:
        port = ports[0]
        print(f"✅ 自动检测到端口: {port.device}  ({port.description})")
        return port.device

    # 多个端口，让用户选择
    print("\n检测到多个 COM 端口:")
    print("-" * 60)
    for i, port in enumerate(ports, 1):
        desc = port.description or "未知设备"
        hwid = port.hwid or ""
        print(f"  [{i}] {port.device:8s} — {desc}")
        if hwid and hwid != "n/a":
            print(f"       {hwid}")
    print("-" * 60)

    while True:
        try:
            choice = input(f"请选择端口 [1-{len(ports)}]: ").strip()
            if not choice:
                continue
            idx = int(choice) - 1
            if 0 <= idx < len(ports):
                return ports[idx].device
            print(f"  请输入 1 到 {len(ports)} 之间的数字")
        except ValueError:
            print("  请输入有效的数字")
        except KeyboardInterrupt:
            print("\n取消操作。")
            sys.exit(0)


def get_wifi_credentials(ssid=None, password=None):
    """交互式获取 WiFi 凭据"""
    print("\n🔧 WiFi 配置")
    print("-" * 40)

    if not ssid:
        ssid = input("WiFi 名称 (SSID): ").strip()
        if not ssid:
            print("❌ SSID 不能为空！")
            sys.exit(1)

    if not password:
        # 用 getpass 隐藏密码输入（如果终端支持的话）
        try:
            password = getpass.getpass("WiFi 密码: ")
        except Exception:
            password = input("WiFi 密码: ").strip()

    return ssid, password


def send_wifi_config(port, ssid, password, monitor=False):
    """通过串口发送 WiFi 配置"""
    cmd = f"WIFI:{ssid}:{password}"

    print(f"\n📡 正在连接到 {port} (波特率 {BAUD_RATE})...")

    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=TIMEOUT)
    except serial.SerialException as e:
        print(f"❌ 无法打开端口 {port}: {e}")
        print("   请检查:")
        print("   - M5StickC Plus 是否已连接")
        print("   - 端口是否被其他程序占用 (如 Arduino IDE 串口监视器)")
        sys.exit(1)

    # 等待串口稳定（ESP32 重启时有延迟）
    time.sleep(0.5)

    # 清空接收缓冲区
    ser.reset_input_buffer()

    # 发送配置命令
    print(f"📤 发送 WiFi 配置...")
    print(f"   SSID:     {ssid}")
    print(f"   密码:     {'*' * len(password)}")
    ser.write((cmd + "\n").encode("utf-8"))
    ser.flush()

    print("\n⏳ 等待设备响应...")
    print("-" * 60)

    # 读取设备响应，最多等待 10 秒
    success = False
    start_time = time.time()

    while time.time() - start_time < 10:
        if ser.in_waiting:
            try:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if line:
                    # 颜色化输出
                    print_colored(line)

                    # 检查是否配置成功
                    if "WiFi configured" in line or "WiFi saved" in line or "WIFI_OK" in line:
                        success = True
                    if "connected" in line.lower() and "wifi" in line.lower():
                        success = True
            except Exception:
                pass
        else:
            time.sleep(0.1)

    print("-" * 60)

    if success:
        print("\n✅ WiFi 配置成功！")
    else:
        print("\n⚠️  已发送配置指令。")
        print("   如果设备屏幕显示了 WiFi 名称，说明配置成功。")
        print("   如果没有反应，请确认设备处于 Boot 等待界面。")

    # 进入串口监听模式
    if monitor:
        print("\n📺 进入串口监听模式 (按 Ctrl+C 退出)")
        print("=" * 60)
        try:
            while True:
                if ser.in_waiting:
                    try:
                        line = ser.readline().decode("utf-8", errors="replace").strip()
                        if line:
                            print_colored(line)
                    except Exception:
                        pass
                else:
                    time.sleep(0.05)
        except KeyboardInterrupt:
            print("\n\n🛑 监听结束。")

    ser.close()
    print(f"端口 {port} 已关闭。")


def print_colored(line):
    """根据日志前缀添加简单的标识符号"""
    if line.startswith("[ERR"):
        print(f"  ❌ {line}")
    elif line.startswith("[WARN") or line.startswith("[BAT]"):
        print(f"  ⚠️  {line}")
    elif line.startswith("[ASR]") or line.startswith("[CMD]"):
        print(f"  🎤 {line}")
    elif line.startswith("[NAV]"):
        print(f"  🧭 {line}")
    elif line.startswith("[SCAN]"):
        print(f"  🔍 {line}")
    elif line.startswith("[MODE]"):
        print(f"  🤖 {line}")
    elif line.startswith("[BOOT]"):
        print(f"  🚀 {line}")
    elif line.startswith("[WIFI]") or "WiFi" in line or "wifi" in line:
        print(f"  📶 {line}")
    else:
        print(f"     {line}")


def main():
    parser = argparse.ArgumentParser(
        description="小虫 (XiaoChong) Rover Bot — WiFi 配置工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python wifi_config.py                          交互式配置
  python wifi_config.py -p COM3                  指定端口
  python wifi_config.py -s MyWiFi -k 12345678   直接指定 WiFi 信息
  python wifi_config.py --monitor                配置后监听串口输出
        """,
    )
    parser.add_argument(
        "-p", "--port",
        help="串口端口 (如 COM3)，不指定则自动检测",
    )
    parser.add_argument(
        "-s", "--ssid",
        help="WiFi 名称 (SSID)",
    )
    parser.add_argument(
        "-k", "--password",
        help="WiFi 密码",
    )
    parser.add_argument(
        "-m", "--monitor",
        action="store_true",
        help="配置完成后进入串口监听模式",
    )

    args = parser.parse_args()

    print("=" * 60)
    print("    🐛 小虫 (XiaoChong) — WiFi 配置工具")
    print("=" * 60)

    # 1. 选择端口
    port = select_port(args.port)

    # 2. 获取 WiFi 信息
    ssid, password = get_wifi_credentials(args.ssid, args.password)

    # 3. 确认
    print(f"\n📋 配置确认:")
    print(f"   端口:  {port}")
    print(f"   SSID:  {ssid}")
    print(f"   密码:  {'*' * len(password)}")

    try:
        confirm = input("\n确认发送? [Y/n]: ").strip().lower()
        if confirm and confirm != "y":
            print("已取消。")
            sys.exit(0)
    except KeyboardInterrupt:
        print("\n已取消。")
        sys.exit(0)

    # 4. 发送
    send_wifi_config(port, ssid, password, monitor=args.monitor)


if __name__ == "__main__":
    main()
