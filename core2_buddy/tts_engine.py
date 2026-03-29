"""
tts_engine.py — Core2 Buddy TTS 语音合成引擎

使用 edge-tts (微软 Neural TTS) 将文本转为 PCM 音频。
- 免费，无需 API Key
- 中文女声: zh-CN-XiaoxiaoNeural
- 输出: PCM 16kHz 16bit mono (可直接通过串口发给 Core2 播放)
"""

import asyncio
import io
import logging
import os

# ── ffmpeg 路径 ────────────────────────────────────────────────────────────
# pydub 导入时 utils.py 会用 subprocess 搜索 PATH 找 ffmpeg/ffprobe，
# 仅设 AudioSegment 属性不够，必须加 PATH 环境变量。
# 优先使用环境变量 FFMPEG_PATH，若未设置则依赖系统 PATH。
FFMPEG_PATH = os.environ.get("FFMPEG_PATH", "")
if FFMPEG_PATH and os.path.isdir(FFMPEG_PATH):
    os.environ["PATH"] = FFMPEG_PATH + os.pathsep + os.environ.get("PATH", "")

import edge_tts
from pydub import AudioSegment

# 显式设置 pydub converter 路径（双保险）
if FFMPEG_PATH and os.path.isdir(FFMPEG_PATH):
    AudioSegment.converter = os.path.join(FFMPEG_PATH, "ffmpeg.exe")
    AudioSegment.ffprobe = os.path.join(FFMPEG_PATH, "ffprobe.exe")

log = logging.getLogger("tts_engine")

# ── Configuration (可通过环境变量覆盖) ──────────────────────────────────────
VOICE = os.environ.get("TTS_VOICE", "zh-CN-XiaoxiaoNeural")
RATE = os.environ.get("TTS_RATE", "+0%")
VOLUME = os.environ.get("TTS_VOLUME", "+0%")
SAMPLE_RATE = 16000               # Core2 I2S 播放采样率 (与固件匹配，不可改)
CHANNELS = 1                      # mono
SAMPLE_WIDTH = 2                  # 16bit


async def text_to_pcm(text: str, sample_rate: int = SAMPLE_RATE) -> bytes:
    """
    文本 → PCM 音频 (int16 LE, mono, 16kHz)
    
    Args:
        text: 要合成的中文文本
        sample_rate: 采样率
    
    Returns:
        PCM 音频数据 bytes
    """
    communicate = edge_tts.Communicate(
        text, voice=VOICE, rate=RATE, volume=VOLUME
    )

    # 收集 MP3 数据 (edge-tts 输出 MP3)
    mp3_data = b""
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            mp3_data += chunk["data"]

    if not mp3_data:
        log.error("edge-tts returned no audio data")
        return b""

    # MP3 → PCM 转换
    audio = AudioSegment.from_mp3(io.BytesIO(mp3_data))
    audio = audio.set_channels(CHANNELS)
    audio = audio.set_frame_rate(sample_rate)
    audio = audio.set_sample_width(SAMPLE_WIDTH)

    pcm = audio.raw_data
    duration_ms = len(pcm) / (sample_rate * SAMPLE_WIDTH * CHANNELS) * 1000
    log.info(f"TTS: '{text[:30]}...' → {len(pcm)} bytes PCM ({duration_ms:.0f}ms)")
    return pcm


def tts_notify(text: str) -> bytes:
    """同步封装: 文本 → PCM"""
    return asyncio.run(text_to_pcm(text))


def tts_task_completed(ws_name: str = "", task_name: str = "") -> bytes:
    """生成任务完成通知语音"""
    return tts_notify("任务已完成，请及时查收。")


def tts_task_error(ws_name: str = "", task_name: str = "") -> bytes:
    """生成任务出错通知语音"""
    return tts_notify("注意，有任务出现问题，请及时查看。")


# ── Quick test ──────────────────────────────────────────────────────────────
if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    print("Testing TTS engine...")
    pcm = tts_notify("工作空间 m5stack toys，任务 Core2 Buddy 设计稿 已完成")
    print(f"Generated {len(pcm)} bytes PCM audio")
    print(f"Duration: {len(pcm) / (SAMPLE_RATE * SAMPLE_WIDTH) * 1000:.0f}ms")
    
    # Save as WAV for verification
    audio = AudioSegment(
        data=pcm,
        sample_width=SAMPLE_WIDTH,
        frame_rate=SAMPLE_RATE,
        channels=CHANNELS
    )
    audio.export("test_tts.wav", format="wav")
    print("Saved test_tts.wav")
