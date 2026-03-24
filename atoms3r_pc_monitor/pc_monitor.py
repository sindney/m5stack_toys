#!/usr/bin/env python3
"""
PC Monitor - 通过 BLE 发送系统状态到 AtomS3R

依赖安装:
    pip install bleak psutil nvidia-ml-py

可选 AMD GPU 支持:
    pip install pyadl

使用方法:
    python pc_monitor.py
"""

import asyncio
import atexit
import sys
from typing import Optional

try:
    import psutil
except ImportError:
    print("请安装 psutil: pip install psutil")
    sys.exit(1)

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("请安装 bleak: pip install bleak")
    sys.exit(1)

# ─── GPU 后端检测 ───
# 支持三种后端，按优先级: NVIDIA (pynvml) > AMD (pyadl) > 无 GPU
GPU_BACKEND = None  # "nvidia" | "amd" | None
_nvml_handle = None  # NVIDIA: 全局 GPU handle（只 init 一次）
_amd_device = None   # AMD: 全局 ADL device 对象

# 1) 尝试 NVIDIA
try:
    import pynvml
    pynvml.nvmlInit()
    _nvml_handle = pynvml.nvmlDeviceGetHandleByIndex(0)
    gpu_name = pynvml.nvmlDeviceGetName(_nvml_handle)
    if isinstance(gpu_name, bytes):
        gpu_name = gpu_name.decode("utf-8")
    GPU_BACKEND = "nvidia"
    print(f"GPU 后端: NVIDIA — {gpu_name}")

    # 程序退出时自动 shutdown（只关一次）
    def _nvml_cleanup():
        try:
            pynvml.nvmlShutdown()
        except Exception:
            pass
    atexit.register(_nvml_cleanup)

except ImportError:
    pass
except Exception as e:
    print(f"警告: NVIDIA GPU 初始化失败: {e}")

# 2) 尝试 AMD (pyadl)
if GPU_BACKEND is None:
    try:
        from pyadl import ADLManager
        _amd_devices = ADLManager.getInstance().getDevices()
        if _amd_devices:
            _amd_device = _amd_devices[0]
            GPU_BACKEND = "amd"
            print(f"GPU 后端: AMD — {_amd_device.adapterName}")
    except ImportError:
        pass
    except Exception as e:
        print(f"警告: AMD GPU 初始化失败: {e}")

if GPU_BACKEND is None:
    print("警告: 未检测到可用 GPU (支持 NVIDIA / AMD)")
    print("      NVIDIA: pip install nvidia-ml-py")
    print("      AMD:    pip install pyadl")

# BLE 配置
DEVICE_NAME_PREFIX = "AtomS3R-Mon"  # 设备名前缀，用于匹配多台设备
SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"

# 更新间隔（秒）
UPDATE_INTERVAL = 1.0


def get_cpu_percent() -> int:
    """获取 CPU 使用率"""
    return int(psutil.cpu_percent(interval=None))


def get_memory_percent() -> int:
    """获取内存使用率"""
    return int(psutil.virtual_memory().percent)


def get_gpu_stats() -> tuple:
    """获取 GPU 利用率和温度 (支持 NVIDIA / AMD)
    
    返回: (gpu_percent, gpu_temp)
    """
    gpu_percent = 0
    gpu_temp = 0

    if GPU_BACKEND == "nvidia":
        try:
            util = pynvml.nvmlDeviceGetUtilizationRates(_nvml_handle)
            gpu_percent = util.gpu
        except Exception:
            pass
        try:
            gpu_temp = pynvml.nvmlDeviceGetTemperature(
                _nvml_handle, pynvml.NVML_TEMPERATURE_GPU
            )
        except Exception:
            pass

    elif GPU_BACKEND == "amd":
        try:
            gpu_percent = _amd_device.getCurrentUsage()
        except Exception:
            pass
        try:
            gpu_temp = _amd_device.getCurrentTemperature()
        except Exception:
            pass

    return gpu_percent, gpu_temp


class PCMonitor:
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.connected = False
        self.running = True
        self.selected_device = None  # 保存用户选择的设备
        
    async def scan_devices(self) -> list:
        """扫描所有匹配的 AtomS3R 设备"""
        print(f"正在扫描 BLE 设备 (前缀: '{DEVICE_NAME_PREFIX}')...")
        
        devices = await BleakScanner.discover(timeout=10.0)
        
        # 过滤出匹配的设备
        matched = []
        for device in devices:
            if device.name and device.name.startswith(DEVICE_NAME_PREFIX):
                matched.append(device)
        
        return matched
    
    async def find_device(self) -> Optional[str]:
        """扫描并选择设备"""
        # 如果之前已选择过设备，尝试直接连接
        if self.selected_device:
            print(f"尝试连接上次选择的设备: {self.selected_device['name']}")
            # 快速扫描验证设备是否存在
            devices = await BleakScanner.discover(timeout=5.0)
            for device in devices:
                if device.address == self.selected_device['address']:
                    print(f"找到设备: {device.name} ({device.address})")
                    return device.address
            print("上次选择的设备不可用，重新扫描...")
            self.selected_device = None
        
        # 扫描设备
        matched = await self.scan_devices()
        
        if not matched:
            print("未找到任何 AtomS3R 设备")
            return None
        
        # 只有一台设备时自动选择
        if len(matched) == 1:
            device = matched[0]
            print(f"找到设备: {device.name} ({device.address})")
            self.selected_device = {'name': device.name, 'address': device.address}
            return device.address
        
        # 多台设备时让用户选择
        print(f"\n发现 {len(matched)} 台 AtomS3R 设备:")
        print("-" * 50)
        for i, device in enumerate(matched, 1):
            print(f"  [{i}] {device.name} ({device.address})")
        print("-" * 50)
        
        while True:
            try:
                choice = input(f"请选择要连接的设备 [1-{len(matched)}]: ").strip()
                if not choice:
                    continue
                idx = int(choice) - 1
                if 0 <= idx < len(matched):
                    device = matched[idx]
                    print(f"\n已选择: {device.name}")
                    self.selected_device = {'name': device.name, 'address': device.address}
                    return device.address
                else:
                    print(f"请输入 1 到 {len(matched)} 之间的数字")
            except ValueError:
                print("请输入有效的数字")
            except KeyboardInterrupt:
                print("\n取消选择")
                return None
    
    async def connect(self, address: str) -> bool:
        """连接到设备"""
        try:
            print(f"正在连接到 {address}...")
            self.client = BleakClient(address)
            await self.client.connect()
            self.connected = True
            print("连接成功!")
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            self.connected = False
            return False
    
    async def disconnect(self):
        """断开连接"""
        if self.client and self.connected:
            try:
                await self.client.disconnect()
            except:
                pass
        self.connected = False
        self.client = None
    
    async def send_stats(self, cpu: int, gpu: int, mem: int, gpu_temp: int, cpu_temp: int):
        """发送系统状态数据"""
        if not self.client or not self.connected:
            return False
        
        try:
            # 打包数据: CPU%, GPU%, MEM%, GPU_TEMP, CPU_TEMP
            data = bytes([
                min(100, max(0, cpu)),
                min(100, max(0, gpu)),
                min(100, max(0, mem)),
                min(255, max(0, gpu_temp)),
                min(255, max(0, cpu_temp))
            ])
            
            await self.client.write_gatt_char(CHARACTERISTIC_UUID, data)
            return True
        except Exception as e:
            print(f"发送失败: {e}")
            self.connected = False
            return False
    
    async def run(self):
        """主循环"""
        # 初始化 CPU 计数器
        psutil.cpu_percent(interval=None)
        
        while self.running:
            # 查找设备
            address = await self.find_device()
            if not address:
                print("5 秒后重试...")
                await asyncio.sleep(5)
                continue
            
            # 连接
            if not await self.connect(address):
                print("5 秒后重试...")
                await asyncio.sleep(5)
                continue
            
            print("\n开始发送系统状态数据...")
            print("按 Ctrl+C 停止\n")
            print("-" * 50)
            
            # 数据发送循环
            while self.connected and self.running:
                try:
                    # 获取系统状态
                    cpu = get_cpu_percent()
                    mem = get_memory_percent()
                    gpu, gpu_temp = get_gpu_stats()
                    cpu_temp = 0  # Windows 上 psutil 无法可靠获取 CPU 温度
                    
                    # 发送
                    success = await self.send_stats(cpu, gpu, mem, gpu_temp, cpu_temp)
                    
                    if success:
                        # 显示状态
                        status = f"CPU: {cpu:3d}% | MEM: {mem:3d}% | GPU: {gpu:3d}%"
                        if gpu_temp > 0:
                            status += f" ({gpu_temp}°C)"
                        
                        print(f"\r{status}", end="", flush=True)
                    else:
                        print("\n连接断开")
                        break
                    
                    await asyncio.sleep(UPDATE_INTERVAL)
                    
                except asyncio.CancelledError:
                    break
                except Exception as e:
                    print(f"\n错误: {e}")
                    break
            
            # 断开连接
            await self.disconnect()
            print("\n\n尝试重新连接...")
            await asyncio.sleep(2)


async def main():
    print("=" * 50)
    print("      PC Monitor for AtomS3R")
    print("=" * 50)
    print()
    
    monitor = PCMonitor()
    
    try:
        await monitor.run()
    except KeyboardInterrupt:
        print("\n\n用户中断，正在退出...")
        monitor.running = False
        await monitor.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
