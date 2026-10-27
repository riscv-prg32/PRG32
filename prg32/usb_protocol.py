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
from typing import BinaryIO, Iterator

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
STATUS = 0x80
RESULT_DATA = 0x81


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
    # Set modem-control lines before opening.  ESP development boards commonly
    # wire DTR/RTS to reset and boot straps; pyserial's defaults can otherwise
    # reset a running device into its ROM downloader merely by opening the port.
    stream = serial.Serial()
    stream.port = port
    stream.baudrate = baud
    stream.timeout = 0.2
    stream.write_timeout = timeout
    stream.dtr = False
    stream.rts = False
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

    def request(self, message_type: int, payload: bytes = b"", *, timeout: float | None = None) -> Frame:
        self.request_id += 1
        request_id = self.request_id
        self.stream.write(encode_frame(Frame(message_type, request_id, payload)))
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            data = self.stream.read(4096)
            for frame in self.decoder.feed(data):
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


def upload_cartridge(
    client: ProtocolClient,
    cartridge: str,
    data: bytes,
    slot: str,
    command: str = "store",
    chunk_size: int = 1024,
) -> dict:
    """Upload a cartridge and optionally request run/debug/performance execution."""
    begin = {
        "name": cartridge_name(data, cartridge),
        "slot": slot,
        "size": len(data),
        "crc32": zlib.crc32(data) & 0xFFFFFFFF,
        "command": command,
    }
    client.request_json(UPLOAD_BEGIN, begin, timeout=15.0)
    for offset in range(0, len(data), chunk_size):
        chunk = data[offset:offset + chunk_size]
        payload = struct.pack("<I", offset) + chunk
        client.require_status(client.request(UPLOAD_DATA, payload, timeout=15.0))
    return client.request_json(UPLOAD_END, {"size": len(data)}, timeout=30.0)


def get_performance(client: ProtocolClient) -> bytes:
    """Retrieve the most recent performance JSON as ordered response chunks."""
    first = client.request(PERFORMANCE_GET, timeout=10.0)
    if first.message_type == STATUS:
        status = json.loads(first.payload.decode("utf-8"))
        raise SystemExit(status.get("error", "performance results unavailable"))
    output = bytearray(first.payload)
    while not (first.flags & 1):
        first = client.request(PERFORMANCE_GET, timeout=10.0)
        output.extend(first.payload)
    return bytes(output)
