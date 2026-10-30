"""PRG32 runtime upload protocol shared by hardware, QEMU, and PRG32-Qt.

Frames are self-synchronising because the eight-byte magic may be searched for
in a stream that also contains ordinary firmware log output.  All integer
fields are little endian and every payload is protected by CRC-32.
"""

from __future__ import annotations

import json
import socket
import struct
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

MAGIC = b"PRG32IO\x00"
VERSION = 1
HEADER = struct.Struct("<8sBBHIII")
MAX_PAYLOAD = 16 * 1024

HELLO = 0x01
UPLOAD_BEGIN = 0x02
UPLOAD_DATA = 0x03
UPLOAD_END = 0x04
RUN = 0x05
DEBUG = 0x06
PERFORMANCE_GET = 0x07
WIFI_SCAN = 0x08
WIFI_SET = 0x09
WIFI_FORGET = 0x0A
KEY = 0x0B
STATUS = 0x80
RESULT_DATA = 0x81

# Header flag bit 0 marks the final response chunk.  On PERFORMANCE_GET
# requests the same bit asks the firmware to restart the result stream.
FLAG_FINAL = 0x0001
FLAG_RESTART = 0x0001
MAX_CHUNK = 4096
COMMANDS = ("store", "run", "debug", "performance")

# Keys of firmware without a HELLO "keys" list. Newer firmware reports its own.
KEYS = ("LEFT", "RIGHT", "UP", "DOWN", "A", "B", "START")
# START and SELECT are two names for one button (PRG32_BTN_SELECT).
KEY_ALIASES = {"SELECT": "START"}
KEY_HOLD_DEFAULT_MS = 200
KEY_HOLD_MAX_MS = 60000


@dataclass(frozen=True)
class Frame:
    message_type: int
    request_id: int
    payload: bytes
    flags: int = 0


def encode_frame(frame: Frame) -> bytes:
    if len(frame.payload) > MAX_PAYLOAD:
        raise ValueError(f"protocol payload exceeds {MAX_PAYLOAD} bytes")
    checksum = zlib.crc32(frame.payload) & 0xFFFFFFFF
    return HEADER.pack(
        MAGIC,
        VERSION,
        frame.message_type,
        frame.flags,
        frame.request_id,
        len(frame.payload),
        checksum,
    ) + frame.payload


class FrameDecoder:
    """Incrementally recover valid protocol frames from a noisy byte stream."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        self._buffer.extend(data)
        frames: list[Frame] = []
        while True:
            start = self._buffer.find(MAGIC)
            if start < 0:
                keep = min(len(self._buffer), len(MAGIC) - 1)
                del self._buffer[: len(self._buffer) - keep]
                break
            if start:
                del self._buffer[:start]
            if len(self._buffer) < HEADER.size:
                break
            magic, version, kind, flags, request_id, size, checksum = HEADER.unpack_from(self._buffer)
            if magic != MAGIC or version != VERSION or size > MAX_PAYLOAD:
                del self._buffer[0]
                continue
            frame_size = HEADER.size + size
            if len(self._buffer) < frame_size:
                break
            payload = bytes(self._buffer[HEADER.size:frame_size])
            del self._buffer[:frame_size]
            if zlib.crc32(payload) & 0xFFFFFFFF != checksum:
                continue
            frames.append(Frame(kind, request_id, payload, flags))
        return frames


class _SocketStream:
    def __init__(self, endpoint: str, timeout: float) -> None:
        host, separator, port_text = endpoint.rpartition(":")
        if not separator or not host:
            raise SystemExit("socket port must use HOST:PORT")
        self._socket = socket.create_connection((host, int(port_text)), timeout=timeout)
        self._socket.settimeout(0.2)

    def write(self, data: bytes) -> int:
        self._socket.sendall(data)
        return len(data)

    def read(self, size: int) -> bytes:
        try:
            return self._socket.recv(size)
        except socket.timeout:
            return b""

    def close(self) -> None:
        self._socket.close()


def open_stream(port: str, baud: int, timeout: float):
    """Open a physical serial port or a TCP stream used by QEMU/PRG32-Qt."""
    if port.startswith("socket://"):
        return _SocketStream(port.removeprefix("socket://"), timeout)
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("USB runtime upload requires pyserial; install the PRG32 package dependencies") from exc
    # Leave DTR and RTS in the state the operating system asserts at open and
    # never change them.  The ESP32-C6 USB Serial/JTAG (and the usual
    # auto-reset circuit on USB-UART boards) resets the chip when RTS is
    # asserted while DTR is not.  Requesting dtr=False/rts=False makes pyserial
    # clear DTR first, passing through exactly that state, so every connection
    # restarted the firmware.  Both lines asserted, and the simultaneous release
    # at close, leave the running firmware untouched (verified on prototype1).
    stream = serial.Serial()
    stream.port = port
    stream.baudrate = baud
    stream.timeout = 0.2
    stream.write_timeout = timeout
    stream.open()
    return stream


class ProtocolClient:
    def __init__(self, stream, timeout: float = 10.0) -> None:
        self.stream = stream
        self.timeout = timeout
        self.decoder = FrameDecoder()
        self.request_id = 0

    def close(self) -> None:
        self.stream.close()

    def _read_available(self) -> bytes:
        """Return bytes as soon as any arrive.

        pyserial's ``read(n)`` waits for n bytes or the 0.2 s port timeout, so
        reading a fixed 4096 bytes added that timeout to every reply.
        """
        waiting = getattr(self.stream, "in_waiting", None)
        if waiting is None:
            return self.stream.read(4096)
        return self.stream.read(waiting or 1)

    def request(
        self,
        message_type: int,
        payload: bytes = b"",
        *,
        timeout: float | None = None,
        flags: int = 0,
    ) -> Frame:
        self.request_id += 1
        request_id = self.request_id
        self.stream.write(encode_frame(Frame(message_type, request_id, payload, flags)))
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            for frame in self.decoder.feed(self._read_available()):
                if frame.request_id == request_id:
                    return frame
        raise SystemExit(f"PRG32 device did not answer protocol request {request_id}")

    def request_json(self, message_type: int, value: dict, *, timeout: float | None = None) -> dict:
        frame = self.request(
            message_type,
            json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8"),
            timeout=timeout,
        )
        try:
            result = json.loads(frame.payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise SystemExit("device returned an invalid protocol JSON response") from exc
        if not result.get("ok", False):
            raise SystemExit(result.get("error", "device rejected protocol request"))
        return result

    @staticmethod
    def require_status(frame: Frame) -> dict:
        if frame.message_type != STATUS:
            raise SystemExit("device returned an unexpected protocol response")
        try:
            result = json.loads(frame.payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise SystemExit("device returned an invalid protocol status") from exc
        if not result.get("ok", False):
            raise SystemExit(result.get("error", "device rejected protocol request"))
        return result


def cartridge_name(data: bytes, fallback: str) -> str:
    if len(data) >= 72 and data[:4] == b"PRG2":
        raw = data[40:72].split(b"\0", 1)[0]
        try:
            name = raw.decode("utf-8").strip()
        except UnicodeDecodeError:
            name = ""
        if name:
            return name
    return Path(fallback).stem


def hello(client: ProtocolClient) -> dict:
    """Return the device capabilities and runtime contract."""
    return client.request_json(HELLO, {})


def upload_cartridge(
    client: ProtocolClient,
    cartridge: str,
    data: bytes,
    slot: str,
    command: str = "store",
    chunk_size: int = 1024,
    progress: Callable[[int, int], None] | None = None,
) -> dict:
    """Upload a cartridge and optionally request run/debug/performance execution.

    ``progress`` is called with ``(received, total)`` after every chunk the
    device acknowledged.  A chunk whose reply is lost is sent once more; the
    firmware acknowledges an identical resend without writing it twice.
    """
    if command not in COMMANDS:
        raise SystemExit(f"unknown upload command {command!r}")
    if not 1 <= chunk_size <= MAX_CHUNK:
        raise SystemExit(f"chunk size must be 1..{MAX_CHUNK} bytes")
    begin = {
        "name": cartridge_name(data, cartridge),
        "slot": slot,
        "size": len(data),
        "crc32": zlib.crc32(data) & 0xFFFFFFFF,
        "command": command,
    }
    # Erasing a 128 KiB slot takes about a second on ESP32-C6 flash.
    client.request_json(UPLOAD_BEGIN, begin, timeout=15.0)
    for offset in range(0, len(data), chunk_size):
        chunk = data[offset:offset + chunk_size]
        payload = struct.pack("<I", offset) + chunk
        try:
            frame = client.request(UPLOAD_DATA, payload, timeout=min(5.0, client.timeout))
        except SystemExit:
            frame = client.request(UPLOAD_DATA, payload, timeout=10.0)
        status = client.require_status(frame)
        if progress is not None:
            progress(int(status.get("received", offset + len(chunk))), len(data))
    return client.request_json(UPLOAD_END, {"size": len(data)}, timeout=30.0)


def get_performance(client: ProtocolClient) -> bytes:
    """Retrieve the most recent performance JSON as ordered response chunks.

    The first request sets the restart flag so a transfer abandoned by an
    earlier host does not leave the firmware in the middle of the document.
    """
    frame = client.request(PERFORMANCE_GET, timeout=10.0, flags=FLAG_RESTART)
    output = bytearray()
    while True:
        if frame.message_type == STATUS:
            client.require_status(frame)
            raise SystemExit("device returned a status instead of performance data")
        if frame.message_type != RESULT_DATA:
            raise SystemExit("device returned an unexpected performance response")
        output.extend(frame.payload)
        if frame.flags & FLAG_FINAL:
            return bytes(output)
        frame = client.request(PERFORMANCE_GET, timeout=10.0)


def require_wifi(client: ProtocolClient) -> dict:
    """Return HELLO, or stop when the firmware has no Wi-Fi messages."""
    info = hello(client)
    if "wifi" not in info.get("capabilities", []):
        raise SystemExit(
            "this PRG32 firmware does not offer Wi-Fi over the device protocol "
            "(QEMU builds and firmware older than October 2026 do not)"
        )
    return info


def wifi_scan(client: ProtocolClient) -> dict:
    """Scan nearby networks; the reply also lists the stored network names."""
    return client.request_json(WIFI_SCAN, {}, timeout=30.0)


def wifi_set(client: ProtocolClient, ssid: str, password: str) -> dict:
    """Store and join a network. The firmware waits up to 20 s for an address
    and restores its previous Wi-Fi configuration if joining fails."""
    return client.request_json(WIFI_SET, {"ssid": ssid, "password": password}, timeout=60.0)


def wifi_forget(client: ProtocolClient, ssid: str) -> dict:
    """Erase a stored network and its password from the device."""
    return client.request_json(WIFI_FORGET, {"ssid": ssid}, timeout=30.0)


def require_keys(client: ProtocolClient) -> tuple[str, ...]:
    """Return the device's key names, or stop when it has no KEY message."""
    info = hello(client)
    if "keys" not in info.get("capabilities", []):
        raise SystemExit(
            "this PRG32 firmware does not accept key commands over the device "
            "protocol (PRG32-Qt and firmware older than this feature do not)"
        )
    return tuple(info.get("keys", KEYS))


def normalize_keys(names: list[str], allowed: tuple[str, ...] = KEYS) -> list[str]:
    """Upper-case key names and resolve aliases, refusing unknown ones."""
    keys = []
    for name in names:
        key = name.strip().upper()
        key = KEY_ALIASES.get(key, key)
        if key not in allowed:
            raise SystemExit(f"unknown key {name!r}; this device accepts: {', '.join(allowed)}")
        keys.append(key)
    return keys


def key_press(client: ProtocolClient, keys: list[str], hold_ms: int = KEY_HOLD_DEFAULT_MS) -> dict:
    """Press keys together. The firmware releases them after ``hold_ms``, once
    a read has returned them, so the reply comes back while they are still down."""
    if not 1 <= hold_ms <= KEY_HOLD_MAX_MS:
        raise SystemExit(f"hold time must be 1..{KEY_HOLD_MAX_MS} ms")
    return client.request_json(KEY, {"action": "press", "keys": keys, "hold_ms": hold_ms})


def key_release(client: ProtocolClient, keys: list[str] | None = None) -> dict:
    """Release the given keys early, or every remote key when none is given."""
    return client.request_json(KEY, {"action": "release", "keys": keys or []})


def key_sequence(
    client: ProtocolClient,
    steps: list[list[str]],
    hold_ms: int = KEY_HOLD_DEFAULT_MS,
    gap_ms: int = 100,
) -> None:
    """Press each step in turn, waiting so a game sees it go down and come up."""
    for index, keys in enumerate(steps):
        key_press(client, keys, hold_ms)
        if index + 1 < len(steps):
            time.sleep((hold_ms + gap_ms) / 1000.0)
