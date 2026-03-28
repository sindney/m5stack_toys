#!/usr/bin/env python3
"""
下载 MaixPy 固件 for UnitV (K210 + OV2640)
优先下载标准版（包含 ws2812 等模块），如果下不到再 fallback 到 minimum 版。
"""
import urllib.request
import os
import sys

DEST = os.path.join(os.path.dirname(__file__), "maixpy_firmware.bin")

# 多个候选下载地址（按优先级排列）
# 优先标准版（包含 ws2812 模块），其次 minimum_with_ide_support
URLS = [
    # Sipeed 下载站 v0.6.3 标准版
    "https://dl.sipeed.com/MAIX/MaixPy/release/master/maixpy_v0.6.3_2_gd8901fd22/maixpy_v0.6.3_2_gd8901fd22.bin",
    # Sipeed 下载站 v0.6.2 标准版
    "https://dl.sipeed.com/MAIX/MaixPy/release/master/maixpy_v0.6.2_27_g4bc5f7b/maixpy_v0.6.2_27_g4bc5f7b.bin",
    # GitHub releases 标准版
    "https://github.com/sipeed/MaixPy-v1/releases/download/v0.6.3/maixpy_v0.6.3_2_gd8901fd22.bin",
    "https://github.com/sipeed/MaixPy-v1/releases/download/v0.6.2/maixpy_v0.6.2_27_g4bc5f7b.bin",
    # Fallback: minimum_with_ide_support（ws2812 模块可能不可用）
    "https://dl.sipeed.com/MAIX/MaixPy/release/master/maixpy_v0.6.3_2_gd8901fd22/maixpy_v0.6.3_2_gd8901fd22_minimum_with_ide_support.bin",
    "https://dl.sipeed.com/MAIX/MaixPy/release/master/maixpy_v0.6.2_27_g4bc5f7b/maixpy_v0.6.2_27_g4bc5f7b_minimum_with_ide_support.bin",
]

def download(url, dest):
    print(f"  Trying: {url}")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        resp = urllib.request.urlopen(req, timeout=30)
        data = resp.read()
        size = len(data)
        print(f"  Downloaded: {size} bytes")
        
        # 验证 — MaixPy 标准版约 3-4MB，minimum 约 700KB
        if size < 400000:
            # 可能是 HTML 错误页面
            if b"<html" in data[:200].lower() or b"<?xml" in data[:200]:
                print(f"  SKIP: got HTML/XML error page ({size} bytes)")
                return False
            print(f"  WARNING: file is small ({size} bytes), but not HTML")
        
        with open(dest, "wb") as f:
            f.write(data)
        print(f"  Saved to: {dest}")
        return True
    except Exception as e:
        print(f"  FAILED: {e}")
        return False

print("=" * 50)
print("  MaixPy Firmware Downloader")
print("=" * 50)

for url in URLS:
    if download(url, DEST):
        print(f"\n[OK] Success! Firmware saved to: {DEST}")
        size = os.path.getsize(DEST)
        print(f"  Size: {size} bytes ({size/1024:.1f} KB)")
        sys.exit(0)

print("\n[FAIL] All download sources failed!")
print("  Please manually download MaixPy minimum firmware from:")
print("  https://dl.sipeed.com/shareURL/MAIX/MaixPy/release/master/")
print("  or https://github.com/sipeed/MaixPy-v1/releases")
print(f"  and save it as: {DEST}")
sys.exit(1)
