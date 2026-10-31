#!/usr/bin/env python3
"""Run the POING wall on several consoles and check it against the protocol.

This is the integration test of the multiplayer protocol
(docs/software/multiplayer_protocol.md). It starts QEMU instances, optionally
uses a connected ESP32-C6, runs the Poing Wall cartridge on all of them, and
then checks two independent sources of evidence:

  1. The traffic: a silent observer joins the room. It publishes nothing, so
     the consoles never see it, and the server relays every snapshot to it.
  2. The screens: each console's real framebuffer shows the ball on the screen
     the traffic says it should be on.

Testing on a board overwrites one cartridge slot; see --board-slot.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from prg32.qemu.launch_qemu import (  # noqa: E402
    DEVICE_PROTOCOL_PORT,
    INSTANCE_PORT_STEP,
    MONITOR_PORT,
)

ROOM = "poing-wall-v2"
SCREEN_W = 320
SCREEN_H = 240
GAME_TOP = 20
FRAMEBUFFER_BYTES = SCREEN_W * SCREEN_H * 2
BALL_RADIUS = 30

# The cartridge's snapshot flags.
FLAG_READY = 1 << 0
FLAG_AUTHORITY = 1 << 1
FLAG_ROSTER_SHIFT = 8

# A follower that has just been elected and the old leader may both claim the
# ball for a short moment; the protocol's snapshots arrive every 50 ms.
HANDOVER_GRACE_S = 0.5
# The ball moves 106 px/s. Between two samples of the leader it should not jump
# further than this, unless the wall changed.
MAX_LEADER_JUMP_PX = 60


class WebSocket:
    """The smallest client of RFC 6455 this test needs: masked text frames out,
    unfragmented text frames in. Standard library only, like the rest of the
    PRG32 tooling."""

    def __init__(self, url: str, timeout: float = 5.0) -> None:
        parsed = urlparse(url)
        if parsed.scheme != "ws":
            raise SystemExit("this test only speaks ws://")
        self.sock = socket.create_connection((parsed.hostname, parsed.port or 80), timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            (
                f"GET {parsed.path or '/'} HTTP/1.1\r\nHost: {parsed.netloc}\r\n"
                "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
            ).encode()
        )
        self.buffer = b""
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed during the upgrade")
            self.buffer += chunk
        header, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        if b" 101 " not in header.split(b"\r\n", 1)[0]:
            raise ConnectionError(f"upgrade refused: {header[:80]!r}")

    def send(self, message: dict) -> None:
        payload = json.dumps(message).encode()
        mask = os.urandom(4)
        masked = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
        if len(payload) < 126:
            head = struct.pack("!BB", 0x81, 0x80 | len(payload))
        else:
            head = struct.pack("!BBH", 0x81, 0x80 | 126, len(payload))
        self.sock.sendall(head + mask + masked)

    def _need(self, count: int) -> bytes:
        while len(self.buffer) < count:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self.buffer += chunk
        data, self.buffer = self.buffer[:count], self.buffer[count:]
        return data

    def receive(self) -> dict | None:
        """Return the next JSON message, or None for a control frame."""
        first, second = self._need(2)
        length = second & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._need(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._need(8))[0]
        payload = self._need(length)
        if first & 0x0F != 1:
            return None
        return json.loads(payload)


@dataclass
class Sample:
    time: float
    player_id: int
    x: int
    y: int
    sprite: int
    flags: int
    frame: int
    platform: str
    device_id: str


@dataclass
class Traffic:
    samples: list[Sample] = field(default_factory=list)
    leaves: list[tuple[float, int]] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    lock: threading.Lock = field(default_factory=threading.Lock)


def observe(url: str, traffic: Traffic, stop: threading.Event) -> None:
    """Join the room silently. Pings keep the server's idle timeout away."""
    ws = WebSocket(url)
    ws.sock.settimeout(1.0)
    ws.send({"type": "join", "signature": ROOM, "protocol": "prg32-multiplayer-1.0"})
    last_ping = time.monotonic()
    while not stop.is_set():
        if time.monotonic() - last_ping > 5:
            ws.send({"type": "ping"})
            last_ping = time.monotonic()
        try:
            message = ws.receive()
        except (socket.timeout, TimeoutError):
            continue
        if message is None:
            continue
        with traffic.lock:
            kind = message.get("type")
            if kind == "peer":
                traffic.samples.append(
                    Sample(
                        time.monotonic(),
                        message["player_id"],
                        message["x"],
                        message["y"],
                        message["sprite"],
                        message["flags"],
                        message["frame"],
                        message["platform"],
                        message["device_id"],
                    )
                )
            elif kind == "leave":
                traffic.leaves.append((time.monotonic(), message["player_id"]))
            elif kind == "error":
                traffic.errors.append(message.get("code", "?"))
    ws.sock.close()


def run_tool(*arguments: str) -> None:
    subprocess.check_call(
        [sys.executable, "-m", "prg32", *arguments],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
    )


def start_instance(instance: int, log_dir: Path) -> subprocess.Popen:
    log = (log_dir / f"qemu{instance}.log").open("wb")
    return subprocess.Popen(
        [sys.executable, "-m", "prg32", "qemu", "run", "--instance", str(instance), "--no-audio"],
        cwd=ROOT,
        stdin=subprocess.DEVNULL,
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )


def wait_for_port(port: int, seconds: float = 30.0) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            socket.create_connection(("127.0.0.1", port), 1).close()
            return
        except OSError:
            time.sleep(0.25)
    raise SystemExit(f"nothing listening on port {port}")


def log_serial(port: int, path: Path, stop: threading.Event) -> None:
    """Keep a console's log. Its UART has one client, so this starts after the
    upload. Noise from the setup menu is dropped."""
    with socket.create_connection(("127.0.0.1", port), 5) as sock, path.open("w") as log:
        sock.settimeout(1.0)
        pending = b""
        while not stop.is_set():
            try:
                chunk = sock.recv(4096)
            except (socket.timeout, TimeoutError):
                continue
            if not chunk:
                return
            pending += chunk
            lines = pending.split(b"\n")
            pending = lines.pop()
            for line in lines:
                if b"=> prg32_" not in line:
                    log.write(f"{time.strftime('%H:%M:%S')} {line.decode('latin1')}\n")
            log.flush()


def monitor_command(port: int, command: str) -> str:
    with socket.create_connection(("127.0.0.1", port), 5) as sock:
        sock.settimeout(1.0)
        sock.sendall(command.encode() + b"\n")
        reply = b""
        time.sleep(0.2)
        try:
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                reply += chunk
        except (socket.timeout, TimeoutError):
            pass
    return reply.decode("latin1")


def read_symbol(elf: Path, symbol: str) -> int:
    output = subprocess.check_output(["riscv32-esp-elf-nm", str(elf)], text=True)
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16)
    raise SystemExit(f"{symbol} not found in {elf}")


def qemu_framebuffer(instance: int, g_fb: int, directory: Path) -> bytes:
    """Read a running instance's RGB565 screen through the QEMU monitor."""
    port = MONITOR_PORT + instance * INSTANCE_PORT_STEP
    reply = monitor_command(port, f"xp /1xw 0x{g_fb:x}")
    address = None
    for line in reply.splitlines():
        if line.strip().startswith(f"{g_fb:016x}:") or line.strip().startswith(f"{g_fb:x}:"):
            address = int(line.split(":")[1].split()[0], 16)
    if address is None:
        raise SystemExit(f"could not read g_fb from the monitor: {reply!r}")
    target = directory / f"qemu{instance}.rgb565"
    target.unlink(missing_ok=True)
    monitor_command(port, f'pmemsave 0x{address:x} {FRAMEBUFFER_BYTES} "{target}"')
    for _ in range(20):
        if target.exists() and target.stat().st_size == FRAMEBUFFER_BYTES:
            return target.read_bytes()
        time.sleep(0.05)
    raise SystemExit(f"no framebuffer dump from QEMU instance {instance}")


def board_framebuffer(url: str) -> bytes:
    """Read the board's screen over HTTP and convert its 24-bit BMP to RGB565."""
    with urllib.request.urlopen(f"{url}/api/screenshot.bmp", timeout=10) as response:
        bmp = response.read()
    offset, = struct.unpack_from("<I", bmp, 10)
    width, height = struct.unpack_from("<ii", bmp, 18)
    bits, = struct.unpack_from("<H", bmp, 28)
    if width != SCREEN_W or abs(height) != SCREEN_H or bits != 24:
        raise SystemExit(f"unexpected screenshot format {width}x{height}x{bits}")
    stride = (width * 3 + 3) // 4 * 4
    pixels = bytearray()
    for row in range(SCREEN_H):
        source = row if height < 0 else SCREEN_H - 1 - row
        start = offset + source * stride
        for x in range(width):
            blue, green, red = bmp[start + x * 3:start + x * 3 + 3]
            pixels += struct.pack("<H", ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3))
    return bytes(pixels)


def ball_center(framebuffer: bytes) -> int | None:
    """Return the x of the middle of the ball's visible pixels, or None.

    The ball alone has saturated pink or cyan pixels. Its exact palette is not
    needed, and the two pink bars at the ends of the wall are left out."""
    columns: dict[int, int] = {}
    for y in range(GAME_TOP + 16, GAME_TOP + 180):
        row = y * SCREEN_W * 2
        for x in range(4, SCREEN_W - 4):
            pixel = framebuffer[row + x * 2] | (framebuffer[row + x * 2 + 1] << 8)
            red = (pixel >> 11) << 3
            green = ((pixel >> 5) & 0x3F) << 2
            blue = (pixel & 0x1F) << 3
            pink = red > 110 and green < red // 2
            cyan = blue > 100 and green > 100 and red < green // 2
            if pink or cyan:
                columns[x] = columns.get(x, 0) + 1
    if sum(columns.values()) < 40:
        return None
    return (min(columns) + max(columns)) // 2


def latest_leader(traffic: Traffic) -> Sample | None:
    with traffic.lock:
        for sample in reversed(traffic.samples):
            if sample.flags & FLAG_AUTHORITY:
                return sample
    return None


@dataclass
class Report:
    checks: list[tuple[str, bool, str]] = field(default_factory=list)

    def check(self, name: str, passed: bool, detail: str) -> None:
        self.checks.append((name, passed, detail))
        print(f"[{'PASS' if passed else 'FAIL'}] {name}: {detail}")

    @property
    def passed(self) -> bool:
        return all(passed for _, passed, _ in self.checks)


def analyze_traffic(traffic: Traffic, consoles: int, report: Report, duration: float) -> dict:
    with traffic.lock:
        samples = list(traffic.samples)
        errors = list(traffic.errors)
        leaves = list(traffic.leaves)

    players: dict[int, list[Sample]] = {}
    for sample in samples:
        players.setdefault(sample.player_id, []).append(sample)
    report.check("console count", len(players) == consoles,
                 f"{len(players)} players seen, {consoles} consoles started")

    devices = {runs[0].device_id for runs in players.values()}
    report.check("distinct public device IDs", len(devices) == len(players),
                 f"{len(devices)} IDs for {len(players)} players")
    platforms = sorted(runs[0].platform for runs in players.values())
    report.check("registered platforms", all(p in ("qemu", "esp32c6") for p in platforms),
                 ", ".join(platforms))

    rates = {}
    worst_gap = 0.0
    stale = 0
    for player, runs in players.items():
        span = runs[-1].time - runs[0].time
        rates[player] = len(runs) / span if span > 0 else 0.0
        for earlier, later in zip(runs, runs[1:]):
            worst_gap = max(worst_gap, later.time - earlier.time)
            if ((later.frame - earlier.frame) & 0xFFFFFFFF) >= 0x80000000:
                stale += 1
    report.check("frame counters advance", stale == 0, f"{stale} went backwards")
    report.check("publish rate within policy",
                 all(0 < rate <= 25 for rate in rates.values()),
                 ", ".join(f"{rate:.1f}/s" for rate in rates.values()) + " (reference 20/s, limit 40/s)")
    report.check("no peer expiry risk", worst_gap < 3.0,
                 f"longest silence {worst_gap * 1000:.0f} ms (peer timeout 3000 ms)")
    report.check("no server errors", not errors, ", ".join(errors) or "none")
    report.check("no peer churn", not leaves, f"{len(leaves)} leave messages: " + ", ".join(
        f"{player} ({players[player][0].platform if player in players else 'unknown'})"
        for _, player in leaves))

    # One authority: at every moment, only one player should claim the ball.
    claims = sorted((s for s in samples if s.flags & FLAG_AUTHORITY), key=lambda s: s.time)
    leaders = sorted({s.player_id for s in claims})
    report.check("one authority", len(leaders) == 1,
                 f"{len(leaders)} players claimed the ball during the run")
    expected_leader = min(players) if players else 0
    report.check("lowest player ID leads", leaders == [expected_leader],
                 f"leader {leaders}, lowest {expected_leader}")

    # One ball that moves continuously and reaches every screen and both ends.
    jumps = 0
    reversals = []
    xs = [s.x for s in claims]
    for earlier, later in zip(claims, claims[1:]):
        if abs(later.x - earlier.x) > MAX_LEADER_JUMP_PX:
            jumps += 1
    direction = 0
    for earlier, later in zip(xs, xs[1:]):
        step = later - earlier
        if step and direction and (step > 0) != (direction > 0):
            reversals.append(earlier)
        if step:
            direction = step
    width = consoles * SCREEN_W
    report.check("ball moves continuously", jumps == 0, f"{jumps} jumps over {MAX_LEADER_JUMP_PX} px")
    screens_reached = sorted({min(x // SCREEN_W, consoles - 1) for x in xs})
    report.check("ball crosses every screen", screens_reached == list(range(consoles)),
                 f"screens reached {screens_reached} of {consoles}")
    if xs:
        report.check("ball bounces at both ends only",
                     min(xs) <= BALL_RADIUS + 8 and max(xs) >= width - BALL_RADIUS - 8
                     and all(x <= BALL_RADIUS + 8 or x >= width - BALL_RADIUS - 8 for x in reversals),
                     f"x from {min(xs)} to {max(xs)} on a {width}-pixel wall, "
                     f"{len(reversals)} reversals")
    rosters = {s.flags >> FLAG_ROSTER_SHIFT for s in samples[-consoles * 20:]}
    report.check("all consoles see the whole wall", rosters == {consoles},
                 f"roster sizes at the end: {sorted(rosters)}")

    return {
        "duration_s": duration,
        "players": {
            str(player): {
                "platform": runs[0].platform,
                "device_id": runs[0].device_id,
                "snapshots": len(runs),
                "rate_per_s": round(rates[player], 2),
            }
            for player, runs in players.items()
        },
        "longest_silence_ms": round(worst_gap * 1000),
        "leader": leaders,
        "x_range": [min(xs), max(xs)] if xs else [],
        "reversals": len(reversals),
        "leave_messages": len(leaves),
        "errors": errors,
    }


def check_screens(
    report: Report,
    traffic: Traffic,
    consoles: dict[str, object],
    screens: int,
    g_fb: int,
    directory: Path,
    sightings_wanted: int = 3,
    timeout_s: float = 120.0,
) -> dict:
    """Find out from the pictures which section of the wall each console shows.

    When a console shows the ball at local x while the traffic says the ball is
    at global x, that console is section round((x_global - x_local) / 320)."""
    seen: dict[str, dict[int, int]] = {name: {} for name in consoles}
    deadline = time.monotonic() + timeout_s
    # The ball needs about 3 s per screen, so a console may wait for it.
    while time.monotonic() < deadline and any(
        sum(counts.values()) < sightings_wanted for counts in seen.values()
    ):
        for name, source in consoles.items():
            before = latest_leader(traffic)
            if isinstance(source, int):
                framebuffer = qemu_framebuffer(source, g_fb, directory)
            else:
                framebuffer = board_framebuffer(str(source))
            after = latest_leader(traffic)
            center = ball_center(framebuffer)
            if center is None or before is None or after is None:
                continue
            global_x = (before.x + after.x) // 2
            section = round((global_x - center) / SCREEN_W)
            seen[name][section] = seen[name].get(section, 0) + 1
            (directory / f"{name}-screen{section}.rgb565").write_bytes(framebuffer)

    sections = {}
    for name, counts in seen.items():
        if counts:
            sections[name] = max(counts, key=lambda section: counts[section])
    report.check("every console showed the ball", len(sections) == len(consoles),
                 ", ".join(f"{name}: {seen[name]}" for name in consoles))
    report.check("each console is one section of the wall",
                 sorted(sections.values()) == list(range(screens)),
                 ", ".join(f"{name}=screen {section + 1}" for name, section in sorted(sections.items())))
    return sections


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--qemu", type=int, default=3, help="number of QEMU instances")
    parser.add_argument("--server", required=True, help="server address, for example ws://192.168.1.10:8081")
    parser.add_argument("--board-port", help="serial port of a connected ESP32-C6")
    parser.add_argument("--board-url", help="HTTP address of that board, for its screenshots")
    parser.add_argument("--board-slot", default="cart7", help="cartridge slot the test overwrites on the board")
    parser.add_argument("--qemu-slot", default="cart0")
    parser.add_argument("--duration", type=float, default=60.0, help="seconds to watch the traffic")
    parser.add_argument("--out", type=Path, required=True, help="directory for the report and screens")
    parser.add_argument("--keep-running", action="store_true", help="leave the consoles running afterwards")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    qemu_cart = ROOT / "cartridges/poingwall/dist/poingwall-qemu.prg32"
    board_cart = ROOT / "cartridges/poingwall/dist/poingwall-esp32c6.prg32"

    processes = []
    traffic = Traffic()
    stop = threading.Event()
    observer = threading.Thread(target=observe, args=(args.server, traffic, stop), daemon=True)
    report = Report()
    consoles: dict[str, object] = {}
    try:
        for instance in range(1, args.qemu + 1):
            processes.append(start_instance(instance, args.out))
            wait_for_port(MONITOR_PORT + instance * INSTANCE_PORT_STEP)
            consoles[f"qemu{instance}"] = instance
        time.sleep(5)  # Let the firmware boot before the protocol is used.

        observer.start()
        for instance in range(1, args.qemu + 1):
            port = DEVICE_PROTOCOL_PORT + instance * INSTANCE_PORT_STEP
            run_tool("esp32c6", "upload", str(qemu_cart), "--port", f"socket://127.0.0.1:{port}",
                     "--command", "run", "--slot", args.qemu_slot)
            threading.Thread(
                target=log_serial,
                args=(port, args.out / f"serial{instance}.log", stop),
                daemon=True,
            ).start()
        if args.board_port:
            run_tool("esp32c6", "upload", str(board_cart), "--port", args.board_port,
                     "--command", "run", "--slot", args.board_slot)
            if args.board_url:
                consoles["board"] = args.board_url

        expected = args.qemu + (1 if args.board_port else 0)
        settle = time.monotonic() + 30
        while time.monotonic() < settle:
            with traffic.lock:
                recent = traffic.samples[-expected * 10:]
            if ({s.player_id for s in recent} and len({s.player_id for s in recent}) == expected
                    and all(s.flags >> FLAG_ROSTER_SHIFT == expected for s in recent)):
                break
            time.sleep(0.25)
        else:
            print("the wall did not form within 30 s; the checks below say how far it got")

        with traffic.lock:
            traffic.samples.clear()
            traffic.leaves.clear()
            traffic.errors.clear()
        started = time.monotonic()
        g_fb = read_symbol(ROOT / "build-qemu/PRG32.elf", "g_fb")
        sections = check_screens(report, traffic, consoles, expected, g_fb, args.out)
        remaining = args.duration - (time.monotonic() - started)
        if remaining > 0:
            time.sleep(remaining)
        summary = analyze_traffic(traffic, expected, report, time.monotonic() - started)
        summary["screens"] = sections
        summary["checks"] = [
            {"name": name, "passed": passed, "detail": detail} for name, passed, detail in report.checks
        ]
        summary["consoles"] = {name: str(source) for name, source in consoles.items()}
        (args.out / "report.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    finally:
        stop.set()
        if not args.keep_running:
            # The launcher and its QEMU share a process group.
            for process in processes:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
    print("PASS" if report.passed else "FAIL")
    return 0 if report.passed else 1


if __name__ == "__main__":
    sys.exit(main())
