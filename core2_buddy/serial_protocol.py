"""
serial_protocol.py — Core2 Buddy 串口帧协议 (Python 端)

帧格式:
  [SYNC(2)] [TYPE(1)] [SEQ(1)] [LEN(2)] [PAYLOAD(N)] [CRC8(1)]
  SYNC:  0xBD 0xBD
  LEN:   uint16 LE
  CRC8:  XOR of TYPE+SEQ+LEN_L+LEN_H+PAYLOAD
"""

import struct
import threading
import time
import json
import logging
from typing import Optional, Callable

import serial
import serial.tools.list_ports

log = logging.getLogger("serial_protocol")

# ── Sync bytes ──────────────────────────────────────────────────────────────
SYNC = b'\xBD\xBD'

# ── Message types: PC → Core2 ──────────────────────────────────────────────
MSG_WORKSPACE_LIST = 0x01
MSG_TASK_LIST      = 0x02
# 0x03 reserved (was MSG_TASK_IMAGE, removed)
MSG_STATUS_CHANGE  = 0x04
MSG_AUDIO_DATA     = 0x05
MSG_HEARTBEAT_PC   = 0x06

# ── Message types: Core2 → PC ──────────────────────────────────────────────
MSG_REQ_WORKSPACES = 0x10
MSG_REQ_TASKS      = 0x11
MSG_REQ_TTS        = 0x12  # Request TTS readout for a task
MSG_ACK            = 0x13
MSG_HEARTBEAT_DEV  = 0x14

# ── Max payload ────────────────────────────────────────────────────────────
MAX_PAYLOAD = 4096   # 4KB per frame; images will be split


def _crc8(data: bytes) -> int:
    """XOR-based CRC8."""
    crc = 0
    for b in data:
        crc ^= b
    return crc


def build_frame(msg_type: int, payload: bytes, seq: int = 0) -> bytes:
    """Build a protocol frame ready to send."""
    length = len(payload)
    header = struct.pack('<BB BBH', 0xBD, 0xBD, msg_type, seq & 0xFF, length)
    crc_data = header[2:] + payload          # TYPE+SEQ+LEN_L+LEN_H+PAYLOAD
    crc = _crc8(crc_data)
    return header + payload + struct.pack('B', crc)


def build_json_frame(msg_type: int, obj, seq: int = 0) -> bytes:
    """Build a frame with JSON payload (auto-encodes to UTF-8)."""
    payload = json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    return build_frame(msg_type, payload, seq)


class SerialLink:
    """Thread-safe serial link with frame parsing and auto-detect."""

    def __init__(self, port: str = "auto", baudrate: int = 460800, timeout: float = 0.05):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._ser: Optional[serial.Serial] = None
        self._lock = threading.Lock()
        self._seq = 0
        self._running = False
        self._rx_thread: Optional[threading.Thread] = None
        self._callbacks: dict[int, Callable] = {}
        self._rx_buf = bytearray()

    # ── Auto-detect ──────────────────────────────────────────────────────
    @staticmethod
    def list_candidate_ports() -> list[str]:
        """List serial ports that look like USB devices (skip COM1 etc)."""
        candidates = []
        for p in serial.tools.list_ports.comports():
            # Skip built-in serial ports (COM1 is usually motherboard)
            desc = (p.description or "").lower()
            hwid = (p.hwid or "").lower()
            # Accept USB serial devices, CP210x, CH340, FTDI, or ESP32
            if any(kw in desc for kw in ("usb", "cp210", "ch340", "ch9102", "ftdi", "esp32", "silicon labs")):
                candidates.append(p.device)
            elif "usb" in hwid or "vid" in hwid:
                candidates.append(p.device)
            elif p.device.upper() not in ("COM1",):
                # Also include non-COM1 ports as fallback
                candidates.append(p.device)
        return candidates

    @staticmethod
    def probe_port(port: str, baudrate: int = 460800, timeout: float = 4.0) -> bool:
        """
        Try to open a port and do a heartbeat handshake with Core2.
        Returns True if we got a valid heartbeat response.
        """
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = baudrate
            ser.timeout = 0.1
            ser.write_timeout = 1.0
            ser.dsrdtr = False
            ser.rtscts = False
            ser.dtr = False
            ser.rts = False
            ser.open()
        except serial.SerialException:
            return False

        try:
            # Flush any garbage
            ser.reset_input_buffer()
            time.sleep(0.3)
            ser.reset_input_buffer()

            # Send heartbeat frames a few times
            hb_frame = build_frame(MSG_HEARTBEAT_PC, b'', seq=0)
            deadline = time.time() + timeout
            buf = bytearray()  # Accumulate across iterations

            while time.time() < deadline:
                ser.write(hb_frame)
                time.sleep(0.2)

                # Read available bytes (accumulate!)
                if ser.in_waiting:
                    buf.extend(ser.read(ser.in_waiting))

                # Look for a valid device heartbeat (0x14) frame in response
                idx = buf.find(SYNC)
                while idx >= 0 and idx + 6 <= len(buf):
                    msg_type = buf[idx + 2]
                    length = struct.unpack_from('<H', buf, idx + 4)[0]
                    total = 6 + length + 1
                    if idx + total <= len(buf):
                        if msg_type == MSG_HEARTBEAT_DEV:
                            log.info(f"Core2 Buddy found on {port}!")
                            return True
                    # Try next SYNC
                    next_idx = buf.find(SYNC, idx + 2)
                    if next_idx < 0:
                        break
                    idx = next_idx
                
                # Trim consumed bytes but keep potential partial SYNC at end
                last_sync = buf.rfind(b'\xBD')
                if last_sync >= 0:
                    buf = buf[last_sync:]
                else:
                    buf.clear()

            return False
        finally:
            try:
                ser.close()
            except Exception:
                pass

    @classmethod
    def auto_detect(cls, baudrate: int = 460800) -> Optional[str]:
        """
        Scan all candidate serial ports and return the first one 
        that responds with a Core2 Buddy heartbeat. Returns None if not found.
        """
        candidates = cls.list_candidate_ports()
        if not candidates:
            log.warning("No USB serial ports found")
            return None

        log.info(f"Scanning ports: {candidates}")
        for port in candidates:
            log.info(f"  Probing {port}...")
            if cls.probe_port(port, baudrate):
                return port
            log.info(f"  {port} — no response")

        return None

    # ── Connection ──────────────────────────────────────────────────────
    def open(self) -> bool:
        try:
            self._ser = serial.Serial()
            self._ser.port = self.port
            self._ser.baudrate = self.baudrate
            self._ser.timeout = self.timeout
            self._ser.write_timeout = 1.0
            self._ser.dsrdtr = False
            self._ser.rtscts = False
            self._ser.dtr = False
            self._ser.rts = False
            self._ser.open()
            self._running = True
            self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self._rx_thread.start()
            log.info(f"Serial opened: {self.port} @ {self.baudrate}")
            return True
        except serial.SerialException as e:
            log.error(f"Failed to open {self.port}: {e}")
            return False

    def close(self):
        self._running = False
        if self._rx_thread:
            self._rx_thread.join(timeout=2)
        if self._ser and self._ser.is_open:
            self._ser.close()
        log.info("Serial closed")

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    # ── Sending ─────────────────────────────────────────────────────────
    def send(self, msg_type: int, payload: bytes = b'') -> bool:
        """Send a frame. Returns True on success."""
        with self._lock:
            if not self.is_open:
                return False
            self._seq = (self._seq + 1) & 0xFF
            frame = build_frame(msg_type, payload, self._seq)
            try:
                self._ser.write(frame)
                return True
            except serial.SerialException as e:
                log.error(f"Send error: {e}")
                return False

    def send_json(self, msg_type: int, obj) -> bool:
        """Send JSON payload."""
        payload = json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        return self.send(msg_type, payload)

    def send_heartbeat(self) -> bool:
        return self.send(MSG_HEARTBEAT_PC)

    # ── Receiving (background thread) ───────────────────────────────────
    def on(self, msg_type: int, callback: Callable):
        """Register a callback for a message type: callback(payload: bytes)"""
        self._callbacks[msg_type] = callback

    def _rx_loop(self):
        """Background receive loop: reads bytes and parses frames."""
        while self._running:
            try:
                if not self.is_open:
                    time.sleep(0.1)
                    continue
                data = self._ser.read(self._ser.in_waiting or 1)
                if data:
                    self._rx_buf.extend(data)
                    self._parse_frames()
            except serial.SerialException:
                log.warning("Serial read error, attempting reconnect...")
                time.sleep(1)
            except Exception as e:
                log.error(f"RX loop error: {e}")
                time.sleep(0.1)

    def _parse_frames(self):
        """Parse complete frames from the receive buffer."""
        while True:
            # Find sync (0xBD 0xBD)
            idx = self._rx_buf.find(SYNC)
            if idx < 0:
                # Keep last byte — it might be the first half of SYNC
                if len(self._rx_buf) > 0 and self._rx_buf[-1] == 0xBD:
                    self._rx_buf = self._rx_buf[-1:]
                else:
                    self._rx_buf.clear()
                return
            if idx > 0:
                self._rx_buf = self._rx_buf[idx:]

            # Need at least header (2 sync + 1 type + 1 seq + 2 len = 6)
            if len(self._rx_buf) < 6:
                return

            msg_type = self._rx_buf[2]
            seq = self._rx_buf[3]
            length = struct.unpack_from('<H', self._rx_buf, 4)[0]

            # Full frame = 6 header + length payload + 1 CRC
            total = 6 + length + 1
            if len(self._rx_buf) < total:
                return

            # Extract and verify CRC
            payload = bytes(self._rx_buf[6:6 + length])
            crc_received = self._rx_buf[6 + length]
            crc_data = bytes(self._rx_buf[2:6]) + payload
            crc_calc = _crc8(crc_data)

            # Consume the frame
            self._rx_buf = self._rx_buf[total:]

            if crc_received != crc_calc:
                log.warning(f"CRC mismatch: got 0x{crc_received:02X}, expected 0x{crc_calc:02X}")
                continue

            # Dispatch
            cb = self._callbacks.get(msg_type)
            if cb:
                try:
                    cb(payload)
                except Exception as e:
                    log.error(f"Callback error for type 0x{msg_type:02X}: {e}")
            else:
                log.debug(f"No handler for type 0x{msg_type:02X} (seq={seq}, len={length})")
