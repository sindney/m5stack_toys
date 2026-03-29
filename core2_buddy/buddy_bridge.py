"""
buddy_bridge.py — Core2 Buddy PC 端 Bridge 主程序

职责:
1. 自动检测 Core2 串口 (扫描 USB 串口 → 心跳握手)
2. 定时扫描 WorkBuddy 本地数据 (工作区 + 任务)
3. 检测任务状态变化 → 推送通知到 Core2
4. 响应 Core2 请求 (工作区列表、任务列表、TTS 朗读)
5. TTS 语音合成 (任务完成/出错时自动播报 + 点击任务朗读)
6. 串口断线自动重连

Usage:
    python buddy_bridge.py                 # 自动检测 Core2 串口
    python buddy_bridge.py --port COM4     # 指定串口
"""

import argparse
import json
import logging
import struct
import threading
import time
from typing import Optional

from serial_protocol import (
    SerialLink,
    MSG_WORKSPACE_LIST, MSG_TASK_LIST,
    MSG_STATUS_CHANGE, MSG_AUDIO_DATA, MSG_HEARTBEAT_PC,
    MSG_REQ_WORKSPACES, MSG_REQ_TASKS, MSG_REQ_TTS,
    MSG_ACK, MSG_HEARTBEAT_DEV,
    MAX_PAYLOAD,
)
from wb_scanner import (
    WorkBuddyScanner, ScanResult, Workspace,
    to_wire_workspaces, to_wire_tasks,
)

log = logging.getLogger("buddy_bridge")

# ── Configuration ──────────────────────────────────────────────────────────
SCAN_INTERVAL = 5.0       # seconds between WorkBuddy data scans
HEARTBEAT_INTERVAL = 3.0  # seconds between heartbeat sends
AUDIO_CHUNK_SIZE = 2048   # bytes per audio data frame
RECONNECT_INTERVAL = 3.0  # seconds between reconnect attempts
DEVICE_TIMEOUT = 10.0     # seconds before declaring device dead


class BuddyBridge:
    """Main bridge: PC ↔ Serial ↔ Core2, with auto-detect and reconnection."""

    def __init__(self, port: str = "auto", baudrate: int = 460800):
        self.requested_port = port   # "auto" or specific like "COM4"
        self.baudrate = baudrate
        self.link: Optional[SerialLink] = None
        self.scanner = WorkBuddyScanner()
        self.last_scan: Optional[ScanResult] = None
        self.prev_task_states: dict[str, str] = {}  # task_key → status
        
        self._last_scan_time = 0.0
        self._last_hb_time = 0.0
        self._device_alive = False
        self._last_device_hb = 0.0
        self._connected = False
        self._tts_busy = False  # True while TTS is generating/sending
        
        # TTS (lazy import, may not have edge-tts)
        self._tts_available = False
        try:
            from tts_engine import tts_task_completed, tts_task_error, tts_notify
            self._tts_completed = tts_task_completed
            self._tts_error = tts_task_error
            self._tts_notify = tts_notify
            self._tts_available = True
            log.info("TTS engine loaded (edge-tts)")
        except ImportError as e:
            log.warning(f"TTS not available: {e}")

    # ── Connection Management ──────────────────────────────────────────
    def _find_and_connect(self) -> bool:
        """Find Core2 and establish connection. Returns True on success."""
        if self.requested_port.lower() == "auto":
            log.info("Auto-detecting Core2...")
            port = SerialLink.auto_detect(self.baudrate)
            if not port:
                return False
        else:
            port = self.requested_port

        self.link = SerialLink(port=port, baudrate=self.baudrate)
        
        if not self.link.open():
            log.error(f"Failed to open {port}")
            self.link = None
            return False

        # Register message handlers
        self.link.on(MSG_REQ_WORKSPACES, self._on_req_workspaces)
        self.link.on(MSG_REQ_TASKS, self._on_req_tasks)
        self.link.on(MSG_REQ_TTS, self._on_req_tts)
        self.link.on(MSG_ACK, self._on_ack)
        self.link.on(MSG_HEARTBEAT_DEV, self._on_device_heartbeat)

        self._connected = True
        self._device_alive = False
        self._last_device_hb = time.time()
        self._last_hb_time = 0.0

        log.info(f"Connected to {port}")
        return True

    def _disconnect(self):
        """Cleanly close current connection."""
        if self.link:
            try:
                self.link.close()
            except Exception:
                pass
            self.link = None
        self._connected = False
        self._device_alive = False
        log.info("Disconnected")

    def start(self):
        """Main loop with auto-detect and reconnection."""
        log.info("Core2 Buddy Bridge starting...")
        log.info(f"Port: {'auto-detect' if self.requested_port.lower() == 'auto' else self.requested_port}")
        
        # Initial data scan (don't need device for this)
        self._do_scan()
        
        try:
            while True:
                if not self._connected:
                    # ── Not connected: try to find device ──
                    if self._find_and_connect():
                        # Connected! Send initial data after a short settle
                        time.sleep(0.3)
                        if self.last_scan:
                            ws_list = to_wire_workspaces(self.last_scan)
                            self.link.send_json(MSG_WORKSPACE_LIST, ws_list)
                    else:
                        log.info(f"Core2 not found, retrying in {RECONNECT_INTERVAL}s...")
                        time.sleep(RECONNECT_INTERVAL)
                        continue

                # ── Connected: main loop ──
                now = time.time()

                # Check if connection is still alive
                if not self.link or not self.link.is_open:
                    log.warning("Serial port closed unexpectedly")
                    self._disconnect()
                    continue

                # Heartbeat
                if now - self._last_hb_time >= HEARTBEAT_INTERVAL:
                    if not self.link.send_heartbeat():
                        log.warning("Failed to send heartbeat, reconnecting...")
                        self._disconnect()
                        continue
                    self._last_hb_time = now

                # Check device timeout (skip during TTS generation)
                if self._tts_busy:
                    self._last_device_hb = now  # keep alive during TTS
                elif self._device_alive and now - self._last_device_hb > DEVICE_TIMEOUT:
                    log.warning("Device heartbeat timeout, reconnecting...")
                    self._disconnect()
                    continue

                # Periodic scan
                if now - self._last_scan_time >= SCAN_INTERVAL:
                    self._do_scan()
                    self._last_scan_time = now

                time.sleep(0.05)

        except KeyboardInterrupt:
            log.info("Shutting down...")
        finally:
            self._disconnect()

    # ── Scanning & Change Detection ─────────────────────────────────────
    def _do_scan(self):
        """Scan WorkBuddy data and detect changes."""
        result = self.scanner.scan()
        
        if self.last_scan is not None:
            changes = self._detect_changes(result)
            for change in changes:
                self._notify_change(change)
        
        self.last_scan = result
        self._update_task_states(result)

    def _detect_changes(self, new_result: ScanResult) -> list[dict]:
        """Compare task states and return list of changes."""
        changes = []
        for ws in new_result.workspaces:
            for conv in ws.conversations:
                for todo in conv.todos:
                    key = f"{conv.conversation_id}:{todo.id}"
                    old_status = self.prev_task_states.get(key)
                    if old_status and old_status != todo.status:
                        changes.append({
                            "task_id": todo.id,
                            "conv_id": conv.conversation_id[:8],
                            "old": old_status,
                            "new": todo.status,
                            "ws_name": ws.name,
                            "task_name": todo.content[:40],
                        })
        return changes

    def _update_task_states(self, result: ScanResult):
        """Update the task state snapshot."""
        self.prev_task_states.clear()
        for ws in result.workspaces:
            for conv in ws.conversations:
                for todo in conv.todos:
                    key = f"{conv.conversation_id}:{todo.id}"
                    self.prev_task_states[key] = todo.status

    def _notify_change(self, change: dict):
        """Send status change notification to Core2 + optional TTS."""
        log.info(f"Task change: [{change['ws_name']}] {change['task_name']}: "
                 f"{change['old']} → {change['new']}")
        
        # Send STATUS_CHANGE to Core2
        self.link.send_json(MSG_STATUS_CHANGE, change)
        
        # TTS notification (in background thread)
        if self._tts_available:
            ws_name = change['ws_name']
            task_name = change['task_name']
            new_status = change['new']
            old_status = change['old']
            
            def _notify_tts_worker():
                try:
                    self._tts_busy = True
                    pcm = None
                    if new_status == 'completed' and old_status == 'in_progress':
                        pcm = self._tts_completed(ws_name, task_name)
                    elif new_status == 'pending':
                        pcm = self._tts_error(ws_name, task_name)
                    if pcm and self._connected and self.link:
                        self._send_audio(pcm)
                except Exception as e:
                    log.error(f"TTS error: {e}")
                finally:
                    self._tts_busy = False
            
            if new_status == 'completed' and old_status == 'in_progress' or new_status == 'pending':
                t = threading.Thread(target=_notify_tts_worker, daemon=True)
                t.start()

    # ── Message Handlers (from Core2) ───────────────────────────────────
    def _on_req_workspaces(self, payload: bytes):
        """Core2 requests workspace list."""
        log.debug("Core2 requested workspace list")
        if self.last_scan:
            ws_list = to_wire_workspaces(self.last_scan)
            self.link.send_json(MSG_WORKSPACE_LIST, ws_list)

    def _on_req_tasks(self, payload: bytes):
        """Core2 requests task list for a workspace."""
        if not payload or not self.last_scan:
            return
        ws_id = payload[0]
        if ws_id >= len(self.last_scan.workspaces):
            return
        
        ws = self.last_scan.workspaces[ws_id]
        tasks = to_wire_tasks(ws)
        data = {"ws": ws_id, "tasks": tasks}
        self.link.send_json(MSG_TASK_LIST, data)
        log.debug(f"Sent {len(tasks)} tasks for workspace {ws.name}")

    def _on_req_tts(self, payload: bytes):
        """Core2 requests TTS readout for a task."""
        try:
            req = json.loads(payload.decode('utf-8'))
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            log.error(f"Failed to parse TTS request: {e}")
            return

        ws_id = req.get("ws", 0)
        task_id = req.get("task_id", "")
        log.info(f"TTS request: ws_id={ws_id}, task_id={task_id!r}")
        
        if not self.last_scan or ws_id >= len(self.last_scan.workspaces):
            return

        ws = self.last_scan.workspaces[ws_id]
        
        # Find the task
        target_todo = None
        for conv in ws.conversations:
            for todo in conv.todos:
                if todo.id == task_id:
                    target_todo = todo
                    break
            if target_todo:
                break

        if not target_todo:
            log.warning(f"Task {task_id!r} not found in ws '{ws.name}'")
            return

        # TTS readout
        if not self._tts_available:
            log.warning("TTS not available, cannot read task")
            return

        try:
            status_text = {"completed": "已完成", "in_progress": "进行中", "pending": "待处理"}.get(
                target_todo.status, target_todo.status)
            
            # 简洁的 TTS 文本：只读任务内容 + 状态
            text = f"{target_todo.content}，{status_text}。"
            log.info(f"TTS readout: {text}")
            
            # 在后台线程中生成 TTS，避免阻塞主循环导致心跳超时
            self._tts_busy = True
            def _tts_worker():
                try:
                    pcm = self._tts_notify(text)
                    if self._connected and self.link:
                        self._send_audio(pcm)
                    else:
                        log.warning("Device disconnected during TTS, audio discarded")
                except Exception as e:
                    log.error(f"TTS readout error: {e}")
                finally:
                    self._tts_busy = False
            
            t = threading.Thread(target=_tts_worker, daemon=True)
            t.start()
        except Exception as e:
            self._tts_busy = False
            log.error(f"TTS readout error: {e}")

    def _on_ack(self, payload: bytes):
        """Core2 acknowledged a message."""
        if len(payload) >= 2:
            acked_type = payload[0]
            acked_seq = payload[1]
            log.debug(f"ACK: type=0x{acked_type:02X}, seq={acked_seq}")

    def _on_device_heartbeat(self, payload: bytes):
        """Core2 is alive."""
        if not self._device_alive:
            log.info("Core2 connected!")
            self._device_alive = True
            # Send initial data
            if self.last_scan:
                ws_list = to_wire_workspaces(self.last_scan)
                self.link.send_json(MSG_WORKSPACE_LIST, ws_list)
        self._last_device_hb = time.time()

    # ── Data transmission helpers ───────────────────────────────────────
    def _send_audio(self, pcm_data: bytes):
        """Send PCM audio data to Core2 in chunks."""
        if not self._connected or not self.link:
            log.warning("Cannot send audio: not connected")
            return
        
        total_chunks = (len(pcm_data) + AUDIO_CHUNK_SIZE - 1) // AUDIO_CHUNK_SIZE
        
        for i in range(total_chunks):
            if not self._connected or not self.link:
                log.warning("Connection lost during audio send, aborting")
                return
            offset = i * AUDIO_CHUNK_SIZE
            chunk = pcm_data[offset:offset + AUDIO_CHUNK_SIZE]
            header = struct.pack('<HH', i, total_chunks)
            self.link.send(MSG_AUDIO_DATA, header + chunk)
            time.sleep(0.005)  # Pace the transmission
        
        log.info(f"Sent {len(pcm_data)} bytes audio in {total_chunks} chunks")


# ── Entry point ─────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Core2 Buddy Bridge")
    parser.add_argument(
        "--port", default="auto",
        help="Serial port (default: auto — scans USB ports and handshakes with Core2)"
    )
    parser.add_argument("--baud", type=int, default=460800, help="Baud rate (default: 460800)")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    args = parser.parse_args()

    level = logging.DEBUG if args.debug else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    bridge = BuddyBridge(port=args.port, baudrate=args.baud)
    bridge.start()


if __name__ == "__main__":
    main()
