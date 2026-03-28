# 🤖 Voice Rover — 语音控制麦轮小车

离线语音控制的麦克纳姆轮小车。使用 Unit ASR (CI-03T) 进行离线语音识别，
Unit TOF 测距传感器进行前方避障，M5StickC Plus 作为主控，RoverC Pro 底盘驱动。

**无需 WiFi、无需云端，开机即用。**

## Architecture

```
                     ┌──────────────┐
     "Hi M Five"     │ Unit ASR     │  ← 离线语音识别
     "forward" ...   │ (CI-03T)     │     预置指令集
                     └──────┬───────┘
                            │ UART 115200 (G32/G33)
                            │ Protocol: 0xAA [CMD] 0x55
                            │
┌──────────────┐  I2C  ┌────┴─────────┐  I2C   ┌──────────────┐
│ Unit TOF     │◄──────│ M5StickC Plus│───────►│ RoverC Pro   │
│ (VL53L0X)   │ 0x29  │  (ESP32 主控) │  0x38  │ (4× Mecanum) │
│ 前方测距     │       │  LCD + 蜂鸣器 │        │ 万向轮底盘   │
└──────────────┘       └──────────────┘        └──────────────┘
```

## 语音指令

先说 **"Hi M Five"** 唤醒 ASR 模块，然后说以下指令：

| 指令 | 动作 | 说明 |
|------|------|------|
| **forward** | 前进 | 持续前进，TOF 自动避障 |
| **backward** | 后退 | 持续后退 |
| **turn left** | 左转 | 模拟汽车转向（旋转 0.8 秒后停止） |
| **turn right** | 右转 | 模拟汽车转向（旋转 0.8 秒后停止） |
| **left** | 左平移 | 麦轮横移（0.6 秒后停止） |
| **right** | 右平移 | 麦轮横移（0.6 秒后停止） |
| **stop** | 停止 | 停止所有运动 |
| **speed up** | 加速 | 速度 +15（上限 100） |
| **speed down** | 减速 | 速度 −15（下限 10） |
| **turn off** | 关机 | 播放关机音效后深度睡眠 |

## 碰撞避障

前进时 TOF 实时检测前方距离：

| TOF 距离 | 行为 |
|----------|------|
| > 300mm | 全速前进 |
| 200–300mm | 按比例减速 |
| < 200mm | **紧急停止** + 蜂鸣警报 |

## Hardware

| Component | Model | Interface | Purpose |
|-----------|-------|-----------|---------|
| **主控** | [M5StickC Plus](https://docs.m5stack.com/en/core/m5stickc_plus) | — | ESP32-PICO, 240×135 LCD, 内置蜂鸣器 |
| **底盘** | [RoverC Pro](https://docs.m5stack.com/en/hat/hat_roverc_pro) | HAT 口 (I2C 0x38) | 4 路麦克纳姆轮驱动 |
| **测距** | [Unit TOF](https://docs.m5stack.com/en/unit/tof) | RoverC Grove (I2C 0x29) | VL53L0X 前方障碍检测 |
| **语音** | [Unit ASR](https://docs.m5stack.com/en/unit/Unit%20ASR) | StickC Plus 底部 Grove (UART 115200) | CI-03T 离线语音识别 |

> **接线说明：** StickC Plus 插在 RoverC Pro 的 HAT 口上。TOF 接 RoverC 的 Grove 口（共享 I2C 总线）。
> Unit ASR 通过 Grove 线接 **StickC Plus 底部的 Grove 口**（G32=RX, G33=TX）。

## Quick Start

### 1. 编译烧录

```bash
# 使用 Arduino CLI
arduino-cli compile --fqbn m5stack:esp32:m5stack_stickc_plus rover_bot
arduino-cli upload -p COM3 --fqbn m5stack:esp32:m5stack_stickc_plus rover_bot

# 或使用 build 脚本
build_upload.bat              # 仅编译
build_upload.bat upload       # 编译 + 烧录（默认 COM3）
build_upload.bat upload COM5  # 指定端口
build_upload.bat monitor      # 打开串口监视器
```

### 2. 使用

1. 接好硬件后上电
2. 屏幕会依次显示 BOOT → SCAN（硬件检测） → READY（就绪）
3. 对着 Unit ASR 说 **"Hi M Five"** 唤醒
4. 然后说指令，例如 **"forward"**、**"turn left"**、**"stop"**
5. 按 StickC Plus 正面 **BtnA** 可紧急停车

## Display States

| Screen | Content |
|--------|---------|
| **BOOT** | "VOICE ROVER v1.0" |
| **SCAN** | 硬件检测结果：RoverC [OK]/[--]、TOF [OK]/[--]、ASR [OK]/[--] |
| **READY** | TOF 距离 + 方向 + 速度 + 硬件状态 + 最近语音指令 |

## Serial Commands

通过串口监视器（115200 波特率）发送：

| Command | Description |
|---------|-------------|
| `SPEED:60` | 设置电机速度（10-100） |
| `SCAN` | 重新扫描硬件 |
| `STATUS` | 输出当前状态 |
| `REBOOT` | 重启设备 |
| `STOP` | 紧急停车 |
| `FWD` / `BWD` / `TL` / `TR` | 调试用运动指令（无需 ASR 模块） |

## File Structure

```
rover_bot/
├── rover_bot.ino               # 主控固件 (M5StickC Plus / ESP32)
├── build_upload.bat            # 编译/烧录/监视器脚本
├── read_serial.ps1             # 串口监视器 (PowerShell)
├── cams3_firmware/             # [废弃] CamS3 5MP 摄像头固件
├── unitv_firmware/             # [废弃] UnitV 摄像头固件
└── README.md
```

## Dependencies

| Library | Platform | Purpose |
|---------|----------|---------|
| M5Unified | StickC Plus | 硬件抽象层 |
| M5GFX | StickC Plus | 显示 & 图形 |
| M5-RoverC | StickC Plus | RoverC Pro 电机驱动 |
| VL53L0X (Pololu) | StickC Plus | TOF 测距 |

## Prerequisites

- [Arduino CLI](https://arduino.github.io/arduino-cli/) 或 Arduino IDE
- M5Stack ESP32 board package (`m5stack:esp32`)
- Arduino 库：M5Unified, M5GFX, M5-RoverC, VL53L0X

## License

MIT — see [LICENSE](../LICENSE)
