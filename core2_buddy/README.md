# Core2 Buddy — WorkBuddy 物理任务看板

将 M5Stack Core2 (+M5GO Bottom2) 变成 WorkBuddy 的物理伴侣设备，实时显示工作区和任务状态。

## 功能

- 📋 **两级任务看板** — Workspace 列表 → Task 列表，触摸导航
- 🔊 **TTS 语音朗读** — 点击任务即朗读内容和状态 (edge-tts 中文女声)
- 🔔 **智能通知** — 任务完成/出错时自动语音播报 + LED 呼吸闪烁
- 🖥️ **赛博朋克 UI** — 黑底青色极简风格，大触摸目标

## 硬件

| 组件 | 说明 |
|------|------|
| M5Stack Core2 | ESP32-D0WDQ6, 320×240 触摸屏, I2S 扬声器, 8MB PSRAM |
| M5GO Bottom2 | 10× SK6812 RGB LED |
| USB-C | 连接 PC, 460800 baud |

## 架构

```
Core2 (固件)          PC (Python Bridge)
┌──────────┐         ┌────────────────────┐
│ 触摸 UI   │  USB    │ buddy_bridge.py    │
│ WS 列表   │◄──────►│                    │
│ Task 列表 │ Serial  │ wb_scanner.py      │← WorkBuddy JSON
│ LED 控制  │ 460800  │ tts_engine.py      │← edge-tts
│ I2S 播放  │         │ serial_protocol.py │
└──────────┘         └────────────────────┘
```

## 文件

| 文件 | 说明 |
|------|------|
| `core2_buddy.ino` | Core2 Arduino 固件 |
| `buddy_bridge.py` | PC 端 Bridge 主程序 |
| `wb_scanner.py` | WorkBuddy 数据扫描器 |
| `tts_engine.py` | TTS 语音合成 (edge-tts) |
| `serial_protocol.py` | 串口帧协议 (Python 端) |
| `compile_upload.bat` | Arduino CLI 编译烧录 (gitignore, 本地生成) |

## 使用

### 1. 安装依赖

```bash
cd core2_buddy
pip install -r requirements.txt
```

TTS 需要 **ffmpeg**，请确保 `ffmpeg` 在系统 PATH 中，或设置环境变量：

```bash
# Windows
set FFMPEG_PATH=C:\path\to\ffmpeg\bin

# macOS/Linux
export FFMPEG_PATH=/usr/local/bin
```

### 2. 编译并烧录固件

需要 [Arduino CLI](https://arduino.github.io/arduino-cli/)，确保 `arduino-cli` 在系统 PATH 中，或设置环境变量 `ARDUINO_CLI` 指向可执行文件路径。

```bash
# 使用脚本（默认 COM4）
compile_upload.bat

# 指定端口
compile_upload.bat COM3
```

> ⚠️ **Core2 烧录注意**: Core2 的 arduino-cli auto-reset 不可靠，直接 upload 通常会报 `Wrong boot mode (0x17)`。  
> `compile_upload.bat` 已内置解决方案：将 DTR/RTS 重置 + 等待 3 秒 + upload **全部在一个 Python 进程中完成**，确保时序紧密。  
> 如果手动烧录，**必须**在同一个进程中完成重置和上传（分开执行会因延迟导致 bootloader 窗口过期）：
> ```bash
> python -c "import serial,time,subprocess,sys; s=serial.Serial('COM4',115200); s.setDTR(False); s.setRTS(False); time.sleep(0.5); s.setDTR(True); s.setRTS(True); time.sleep(0.5); s.setDTR(False); s.setRTS(False); time.sleep(0.5); s.close(); time.sleep(3); r=subprocess.run(['arduino-cli','upload','--fqbn','m5stack:esp32:m5stack_core2','--port','COM4','./']); sys.exit(r.returncode)"
> ```

### 3. 运行 Bridge

```bash
# 自动检测串口
python buddy_bridge.py

# 指定串口
python buddy_bridge.py --port COM4

# 指定串口 + 调试模式
python buddy_bridge.py --port COM4 --debug
```

## 配置

以下参数可通过**环境变量**配置，无需修改代码：

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `FFMPEG_PATH` | *(系统 PATH)* | ffmpeg 可执行文件所在目录 |
| `ARDUINO_CLI` | `arduino-cli` | Arduino CLI 可执行文件路径 |
| `TTS_VOICE` | `zh-CN-XiaoxiaoNeural` | edge-tts 语音名称 ([可选语音列表](https://learn.microsoft.com/azure/ai-services/speech-service/language-support#neural-voices)) |
| `TTS_RATE` | `+0%` | TTS 语速 (如 `+20%`, `-10%`) |
| `TTS_VOLUME` | `+0%` | TTS 音量 (如 `+50%`, `-20%`) |

Bridge 命令行参数：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--port` | `auto` | 串口 (auto = 自动扫描 USB 串口) |
| `--baud` | `460800` | 波特率 (需与固件匹配) |
| `--debug` | *(off)* | 开启调试日志 |

## 交互方式

| 操作 | 效果 |
|------|------|
| 点击 Workspace | 进入 Task 列表 |
| 点击 Task | TTS 朗读任务内容+状态 |
| 播放完成后点击屏幕 | 重新播放 |
| 点击 Header (< 标题) | 返回上一级 |
| BACK 按钮 | 返回上一级 |
| REFRESH 按钮 | 刷新当前列表 |

## 自动通知

| 触发条件 | 效果 |
|----------|------|
| 任务 in_progress → completed | 🟢 绿色呼吸灯 + TTS 播报 |
| 任务变为 pending (出问题) | 🔴 红色呼吸灯 + TTS 播报 |

## 依赖

### Python
- pyserial, edge-tts, pydub
- ffmpeg (TTS 的 MP3→PCM 转换需要)

### Arduino
- M5Unified, M5GFX, FastLED, ArduinoJson

## License

MIT
