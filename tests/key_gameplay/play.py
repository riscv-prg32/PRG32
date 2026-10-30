#!/usr/bin/env python3
"""Play the key gameplay cartridge with key commands and check every result.

The cartridge draws its state as a strip of white and black cells. This script
reads the strip from a screenshot after each step, so it checks what the game
itself computed, on the screen it really drew.

ESP32-C6:
    python3 tests/key_gameplay/play.py --port /dev/cu.usbmodem101 --url http://192.168.4.1

QEMU (start it with -monitor tcp:127.0.0.1:5556,server=on,wait=off):
    python3 tests/key_gameplay/play.py --port socket://127.0.0.1:5555 \
        --qemu-monitor 127.0.0.1:5556 --qemu-elf build-qemu/PRG32.elf

Uploading overwrites --slot, cart7 by default. Save it first if it matters.
"""

from __future__ import annotations

import argparse
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from prg32.usb_protocol import (  # noqa: E402
    ProtocolClient,
    key_press,
    key_release,
    open_stream,
    upload_cartridge,
)

# Must match game.c.
FIELDS = ("state", "x", "y", "score", "lives", "moves", "blocked", "a_count", "b_count",
          "start_count", "chord_count", "shots", "held_frames", "ignored", "frame")
TITLE, PLAY, PAUSE, WIN, OVER = range(5)
STRIP_X, STRIP_Y, BIT_W, ROW_H, MAGIC = 184, 0, 8, 4, 0xC5A3
VIEWPORT_Y = 20
SCREEN_W, SCREEN_H = 320, 240


class DeviceScreen:
    """White cells of the screen read from the firmware's screenshot endpoint."""

    def __init__(self, url: str) -> None:
        self.url = url.rstrip("/") + "/api/screenshot.bmp"

    def lit(self, x: int, y: int, data: bytes) -> bool:
        offset = 54 + (SCREEN_H - 1 - y) * SCREEN_W * 3 + x * 3
        return sum(data[offset:offset + 3]) > 3 * 128

    def grab(self) -> bytes:
        data = subprocess.run(["curl", "-s", "-m", "12", self.url], capture_output=True).stdout
        if len(data) != 54 + SCREEN_W * SCREEN_H * 3:
            raise RuntimeError(f"screenshot had {len(data)} bytes")
        return data


class QemuScreen:
    """White cells of the QEMU panel framebuffer read through the monitor."""

    def __init__(self, monitor: str, elf: str) -> None:
        host, _, port = monitor.rpartition(":")
        self.address = (host, int(port))
        nm = shutil.which("riscv32-esp-elf-nm") or "riscv32-esp-elf-nm"
        symbols = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
        pointer = next(line.split()[0] for line in symbols.splitlines() if line.endswith(" g_fb"))
        self.folder = tempfile.mkdtemp(prefix="prg32-key-")
        self.framebuffer = struct.unpack("<I", self._save(int(pointer, 16), 4))[0]

    def _save(self, address: int, size: int) -> bytes:
        path = Path(self.folder) / "memory.bin"
        path.unlink(missing_ok=True)
        with socket.create_connection(self.address) as monitor:
            time.sleep(0.1)
            monitor.sendall(f'pmemsave {address} {size} "{path}"\n'.encode())
            time.sleep(0.3)
        return path.read_bytes()

    def lit(self, x: int, y: int, data: bytes) -> bool:
        offset = (y * SCREEN_W + x) * 2
        return (data[offset] | data[offset + 1] << 8) > 0x8000

    def grab(self) -> bytes:
        return self._save(self.framebuffer, SCREEN_W * SCREEN_H * 2)


class Game:
    def __init__(self, client: ProtocolClient, screen) -> None:
        self.client = client
        self.screen = screen
        self.passed = 0
        self.failed = 0

    def read(self) -> dict[str, int] | None:
        """Return the drawn state, or None for a screenshot taken mid-frame."""
        data = self.screen.grab()
        rows = []
        for row in range(len(FIELDS) + 2):
            y = VIEWPORT_Y + STRIP_Y + row * ROW_H + ROW_H // 2
            rows.append(sum(self.screen.lit(STRIP_X + bit * BIT_W + BIT_W // 2, y, data) << bit
                            for bit in range(16)))
        check = MAGIC
        for value in rows[1:-1]:
            check ^= value
        if rows[0] != MAGIC or rows[-1] != check:
            return None
        return dict(zip(FIELDS, rows[1:-1]))

    def state(self) -> dict[str, int]:
        for _ in range(8):
            state = self.read()
            if state is not None:
                return state
        raise SystemExit("could not read the cartridge state strip from the screen")

    def check(self, name: str, wait: float = 0.0, **expected: int) -> dict[str, int]:
        if wait:
            time.sleep(wait)
        state = self.state()
        wrong = {key: (state[key], value) for key, value in expected.items() if state[key] != value}
        if wrong:
            self.failed += 1
            print(f"FAIL {name}: (got, wanted) {wrong}")
        else:
            self.passed += 1
            print(f"PASS {name}")
        return state

    def tap(self, *steps: str, hold_ms: int = 80, gap: float = 0.25) -> None:
        for step in steps:
            key_press(self.client, step.split("+"), hold_ms)
            time.sleep(hold_ms / 1000.0 + gap)


def play(game: Game) -> None:
    game.check("title screen after loading", state=TITLE)
    game.tap("A", "LEFT")
    game.check("title ignores other keys", state=TITLE, ignored=2)
    game.tap("START")
    game.check("START begins the game", state=PLAY, x=0, y=0, lives=3, score=0, moves=0, ignored=0)

    # Each press is exactly one move: none lost, none repeated.
    game.tap("LEFT")
    game.check("wall of the grid blocks a move", x=0, y=0, moves=1, blocked=1)
    game.tap("RIGHT")
    game.check("first coin", x=1, y=0, score=10, moves=2, blocked=1)
    game.tap("RIGHT")
    game.check("inner wall blocks a move", x=1, y=0, moves=3, blocked=2)
    game.tap("DOWN", "DOWN", "DOWN")
    game.check("three presses are three moves", x=1, y=3, moves=6)
    game.tap("RIGHT", "RIGHT")
    game.check("second coin", x=3, y=3, score=20, moves=8)

    # Holding a key is one move but counts every frame it stays down.
    before = game.state()
    key_press(game.client, ["RIGHT"], 2000)
    time.sleep(2.6)
    after = game.check("long hold moves once", x=4, y=3, moves=9)
    frames = after["frame"] - before["frame"]
    held = after["held_frames"] - before["held_frames"]
    print(f"     hold of 2000 ms: RIGHT down for {held} of {frames} frames")
    game.passed += held > 0
    game.failed += held <= 0

    # Early release through the protocol.
    key_press(game.client, ["LEFT"], 30000)
    time.sleep(0.3)
    key_release(game.client)
    time.sleep(0.3)
    game.check("release ends a long hold", x=3, y=3, moves=10)
    game.tap("RIGHT", hold_ms=40)
    game.check("the key works again after release", x=4, y=3, moves=11)

    # A level-sensitive action: hold A to charge, release to fire.
    key_press(game.client, ["A"], 800)
    time.sleep(1.2)
    game.check("held A charges, release fires", a_count=1, shots=1, score=25)
    game.tap("A", hold_ms=1, gap=0.4)
    game.check("short A press does not charge", a_count=2, shots=1, score=25)

    # Keys of one request reach the game in the same frame.
    game.tap("A+B", hold_ms=200)
    game.check("A+B arrives as one chord", chord_count=1, a_count=2, b_count=0, state=PLAY)

    # Pause: the game state stops responding.
    game.tap("B")
    game.check("B pauses", state=PAUSE, b_count=1)
    game.tap("UP", "DOWN", "LEFT")
    game.check("paused game ignores moves", state=PAUSE, x=4, y=3, moves=11, ignored=3)
    game.tap("START")
    game.check("START resumes", state=PLAY)
    game.tap("START")
    game.check("START pauses", state=PAUSE)
    game.tap("B")
    game.check("B resumes", state=PLAY, b_count=2)

    # Hazard: a life is lost and the player goes back to the start.
    game.tap("UP", "UP", "UP")
    game.check("hazard costs a life", x=0, y=0, lives=2, score=25, moves=14, state=PLAY)

    # Finish the level along the only remaining route.
    game.tap("RIGHT", "DOWN", "DOWN", "DOWN", "RIGHT", "RIGHT", "RIGHT", "RIGHT", "UP", "UP")
    game.check("third coin wins the game", state=WIN, x=5, y=1, score=35, lives=2, moves=24)
    game.tap("LEFT")
    game.check("won game ignores moves", state=WIN, x=5, y=1, moves=24, ignored=4)
    game.tap("START")
    game.check("START returns to the title", state=TITLE)

    # Second game: lose it.
    game.tap("START")
    game.check("a new game starts clean", state=PLAY, x=0, y=0, score=0, lives=3, moves=0)
    game.tap("DOWN", "DOWN")
    game.check("two hazards leave one life", state=PLAY, lives=1, x=0, y=0, moves=2)
    game.tap("DOWN")
    game.check("last life ends the game", state=OVER, lives=0, moves=3)
    game.tap("START", "START")
    game.check("a third game starts after game over", state=PLAY, lives=3, moves=0)

    # Shortest presses, sent as fast as a frame allows.
    frame_ms = measure_frame_ms(game)
    # A 1 ms press needs up to two frames to be seen going down and coming up.
    gap = max(frame_ms * 2.5, 30.0) / 1000.0
    moves = game.state()["moves"]
    for _ in range(10):
        game.tap("RIGHT", hold_ms=1, gap=gap)
        game.tap("LEFT", hold_ms=1, gap=gap)
    game.check(f"20 presses of 1 ms, {gap * 1000:.0f} ms apart, are 20 moves",
               x=0, y=0, moves=moves + 20, wait=0.3)


def measure_frame_ms(game: Game) -> float:
    first = game.state()["frame"]
    start = time.monotonic()
    time.sleep(1.0)
    second = game.state()["frame"]
    frame_ms = (time.monotonic() - start) * 1000.0 / max(1, (second - first) & 0xffff)
    print(f"     about {frame_ms:.0f} ms per frame")
    return frame_ms


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True)
    parser.add_argument("--url", help="device URL, for its screenshot endpoint")
    parser.add_argument("--qemu-monitor", help="HOST:PORT of the QEMU monitor")
    parser.add_argument("--qemu-elf", default="build-qemu/PRG32.elf")
    parser.add_argument("--slot", default="cart7")
    parser.add_argument("--cartridge", required=True, help="keyplay.prg32 built for the target")
    args = parser.parse_args()
    screen = DeviceScreen(args.url) if args.url else QemuScreen(args.qemu_monitor, args.qemu_elf)
    client = ProtocolClient(open_stream(args.port, 115200, 10.0), 10.0)
    try:
        data = Path(args.cartridge).read_bytes()
        upload_cartridge(client, args.cartridge, data, args.slot, command="run", chunk_size=4096)
        time.sleep(1.0)
        game = Game(client, screen)
        play(game)
    finally:
        client.close()
    print(f"{game.passed} passed, {game.failed} failed")
    return 1 if game.failed else 0


if __name__ == "__main__":
    sys.exit(main())
