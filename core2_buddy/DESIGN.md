# Core2 Buddy — 设计概要

## 项目定位

将 M5Stack Core2 (+M5GO Bottom2) 打造为 WorkBuddy 的**物理任务看板**：
- 📋 显示所有工作空间和任务状态（两级列表）
- 🔊 点击任务 TTS 语音朗读内容和状态
- 🔔 任务状态变化时 LED 闪烁 + TTS 语音提醒

**TTS 方案**：`edge-tts` + 中文女声 `zh-CN-XiaoxiaoNeural`（微软 Neural TTS，免费）

---

详细设计见 WorkBuddy Artifact: core2_buddy_design_v1.md
