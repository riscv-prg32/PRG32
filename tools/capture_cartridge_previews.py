#!/usr/bin/env python3
"""Capture real PRG32 cartridge video and PCM audio from QEMU."""

from __future__ import annotations

import argparse
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RATE = 22_050
AUDIO_PORT = 4321
CONSOLE_PORT = 4322
CARTRIDGES = {
    "bachdemo": ("cartridges/bachdemo/dist/bach-stereo-showcase-qemu.prg32",
                 "cartridges/bachdemo/assets/preview.mp4", ()),
    "blackjack": ("cartridges/blackjack/dist/store/blackjack-qemu.prg32",
                  "cartridges/blackjack/preview.mp4",
                  tuple((second, "j") for second in (1, 3, 6, 9, 12, 15, 18, 21, 24, 27))),
    "devicedemo": ("cartridges/devicedemo/dist/devicedemo-qemu.prg32",
                   "cartridges/devicedemo/assets/preview.mp4",
                   tuple((0.5 + i * 0.7, "d") for i in range(7)) + ((6, "j"),)),
    "poing": ("cartridges/poing/dist/poing-qemu-core.prg32",
              "cartridges/poing/assets/preview.mp4",
              ((4, "d"), (7, "j"), (12, "w"), (18, "a"), (23, "j"))),
}


def connect_tcp(port: int, process: subprocess.Popen, timeout: float = 15) -> socket.socket:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited early with status {process.returncode}")
        sock = socket.socket()
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            sock.connect(("127.0.0.1", port))
            return sock
        except ConnectionRefusedError:
            sock.close()
            time.sleep(0.1)
    raise TimeoutError(f"timed out connecting to QEMU port {port}")


def find_qemu_window(process: subprocess.Popen, timeout: float = 15) -> str:
    """Return the CoreGraphics window ID owned by this QEMU process."""
    source = f'''import Foundation
import CoreGraphics
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
                                         kCGNullWindowID)! as NSArray
for case let window as NSDictionary in windows {{
    if window[kCGWindowOwnerPID] as? Int == {process.pid},
       let number = window[kCGWindowNumber] as? Int {{
        print(number)
        break
    }}
}}
'''
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = subprocess.run(["/usr/bin/swift", "-e", source], check=True,
                                capture_output=True, text=True)
        window_id = result.stdout.strip()
        if window_id:
            return window_id
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited early with status {process.returncode}")
        time.sleep(0.2)
    raise TimeoutError("timed out waiting for the QEMU SDL window")


def send_key(console: socket.socket, key: str) -> None:
    """Send a 90 ms pulse that reliably crosses a cartridge update boundary."""
    for _ in range(3):
        console.sendall(key.encode())
        time.sleep(0.03)


def audio_worker(process: subprocess.Popen, recording: threading.Event,
                 complete: threading.Event, samples: bytearray, duration: float) -> None:
    sock = connect_tcp(AUDIO_PORT, process)
    chunk_size = 441 * 2
    target_size = round(duration * RATE) * 2
    try:
        sock.sendall(b"K")
        next_credit = time.monotonic() + 0.02
        while process.poll() is None and len(samples) < target_size:
            data = bytearray()
            while len(data) < chunk_size:
                try:
                    part = sock.recv(chunk_size - len(data))
                except (ConnectionResetError, OSError):
                    return
                if not part:
                    return
                data.extend(part)
            if recording.is_set():
                samples.extend(data[:target_size - len(samples)])
            delay = next_credit - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            next_credit += 0.02
            try:
                sock.sendall(b"K")
            except (BrokenPipeError, ConnectionResetError):
                return
    finally:
        sock.close()
        if len(samples) == target_size:
            complete.set()


def console_worker(console: socket.socket, transcript: bytearray,
                   stopping: threading.Event) -> None:
    console.settimeout(0.2)
    while not stopping.is_set():
        try:
            data = console.recv(4096)
        except TimeoutError:
            continue
        if not data:
            return
        transcript.extend(data)


def capture_audio_only(command: list[str], key_events: tuple[tuple[float, str], ...],
                       warmup: float, duration: float) -> bytearray:
    """Replay without screen recording to capture the complete real PCM stream.

    Keep the SDL display active: the firmware rendering path is part of the
    cartridge's normal execution, and QEMU's ``-display none`` mode can stall
    that path.  Omitting only the macOS recorder removes the competing workload
    while preserving the same firmware, cartridge, timing, and input sequence.
    """
    audio_command = list(command)
    samples, transcript = bytearray(), bytearray()
    recording, complete, stopping = (threading.Event(), threading.Event(),
                                      threading.Event())
    process = subprocess.Popen(audio_command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    thread = threading.Thread(target=audio_worker,
                              args=(process, recording, complete, samples, duration),
                              daemon=True)
    thread.start()
    console = connect_tcp(CONSOLE_PORT, process)
    console_thread = threading.Thread(target=console_worker,
                                      args=(console, transcript, stopping), daemon=True)
    console_thread.start()
    try:
        time.sleep(warmup)
        recording.set()
        event = 0
        deadline = time.monotonic() + duration * 3
        while not complete.is_set() and time.monotonic() < deadline:
            elapsed = len(samples) / 2 / RATE
            while event < len(key_events) and key_events[event][0] <= elapsed:
                send_key(console, key_events[event][1])
                event += 1
            time.sleep(0.01)
        if not complete.is_set():
            raise RuntimeError("audio-only QEMU replay did not complete")
        return samples
    finally:
        stopping.set()
        console_thread.join(timeout=1)
        console.close()
        process.terminate()
        process.wait(timeout=10)
        thread.join(timeout=2)


def capture(name: str, duration: float, fps: int, warmup: float, ffmpeg: str,
            verbose_console: bool = False) -> None:
    cartridge_name, output_name, key_events = CARTRIDGES[name]
    cartridge, output = ROOT / cartridge_name, ROOT / output_name
    if not cartridge.exists():
        raise SystemExit(f"missing {cartridge}; build the QEMU cartridge first")
    for required in (ROOT / "build-qemu/qemu_flash.bin", ROOT / "build-qemu/qemu_efuse.bin"):
        if not required.exists():
            raise SystemExit(f"missing {required}; run `python3 -m prg32 qemu build`")

    with tempfile.TemporaryDirectory(prefix=f"prg32-{name}-capture-") as temp_name:
        temp = Path(temp_name)
        flash, efuse = temp / "flash.bin", temp / "efuse.bin"
        raw_video = temp / "qemu-window.mov"
        shutil.copy2(ROOT / "build-qemu/qemu_flash.bin", flash)
        shutil.copy2(ROOT / "build-qemu/qemu_efuse.bin", efuse)
        subprocess.run(["python3", "-m", "prg32", "qemu", "upload", str(cartridge),
                        "--flash", str(flash)], cwd=ROOT, check=True)
        command = [
            "qemu-system-riscv32", "-M", "esp32c3", "-m", "4M",
            "-drive", f"file={flash},if=mtd,format=raw",
            "-drive", f"file={efuse},if=none,format=raw,id=efuse",
            "-global", "driver=nvram.esp32c3.efuse,property=drive,value=efuse",
            "-global", "driver=timer.esp32c3.timg,property=wdt_disable,value=true",
            "-nic", "user,model=open_eth", "-display", "sdl",
            "-monitor", "none",
            "-serial", f"tcp::{CONSOLE_PORT},server=on,wait=off,nodelay=on",
            "-serial", f"tcp::{AUDIO_PORT},server=on,wait=on,nodelay=on",
        ]
        samples, transcript = bytearray(), bytearray()
        recording, audio_complete, stopping = (threading.Event(), threading.Event(),
                                                threading.Event())
        with (temp / "qemu.log").open("wb") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            thread = threading.Thread(target=audio_worker,
                                      args=(process, recording, audio_complete, samples, duration),
                                      daemon=True)
            thread.start()
            console = connect_tcp(CONSOLE_PORT, process)
            console_thread = threading.Thread(target=console_worker,
                                              args=(console, transcript, stopping),
                                              daemon=True)
            console_thread.start()
            window_id = find_qemu_window(process)
            try:
                # The clean temporary flash contains only cart0, so firmware
                # autoloads it. Warm up before recording the game playfield.
                time.sleep(warmup)
                start = time.monotonic()
                recording.set()
                video = subprocess.Popen([
                    "/usr/sbin/screencapture", "-v", f"-V{duration:g}", "-x", "-o",
                    f"-l{window_id}", str(raw_video),
                ])
                event = 0
                while video.poll() is None:
                    elapsed = time.monotonic() - start
                    while event < len(key_events) and key_events[event][0] <= elapsed:
                        send_key(console, key_events[event][1])
                        event += 1
                    time.sleep(0.02)
                if video.returncode != 0 or not raw_video.exists():
                    raise RuntimeError("macOS window video capture failed")
                audio_complete.wait(3)
            finally:
                stopping.set()
                console_thread.join(timeout=1)
                console.close()
                process.terminate()
                process.wait(timeout=10)
                thread.join(timeout=2)
        if verbose_console:
            sys.stderr.write(transcript[-50_000:].decode("utf-8", errors="replace"))

        audio = temp / "audio.wav"
        expected = round(duration * RATE) * 2
        if len(samples) < RATE * 2:
            raise RuntimeError(f"audio capture too short: {len(samples)}/{expected} bytes")
        actual_audio_duration = len(samples) / 2 / RATE
        if abs(actual_audio_duration - duration) > 0.25:
            print(f"replaying {name} without display capture for complete PCM "
                  f"({actual_audio_duration:.3f}s received during video pass)")
            samples = capture_audio_only(command, key_events, warmup, duration)
        with wave.open(str(audio), "wb") as wav:
            wav.setparams((1, 2, RATE, len(samples) // 2, "NONE", "not compressed"))
            wav.writeframes(samples)
        subprocess.run([
            ffmpeg, "-y", "-loglevel", "error", "-i", str(raw_video),
            "-i", str(audio), "-t", str(duration),
            "-vf", "crop=iw:200:0:48,scale=320:200:flags=neighbor",
            "-r", str(fps), "-af", "apad",
            "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "96k", "-movflags", "+faststart", str(output),
        ], check=True)
    print(f"captured {name}: {output.relative_to(ROOT)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("names", nargs="*", choices=CARTRIDGES)
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--fps", type=int, default=30,
                        help="encoded output frame rate (default: 30)")
    parser.add_argument("--warmup", type=float, default=7)
    parser.add_argument("--ffmpeg")
    parser.add_argument("--verbose-console", action="store_true")
    args = parser.parse_args()
    if args.ffmpeg:
        ffmpeg = args.ffmpeg
    else:
        try:
            import imageio_ffmpeg
        except ImportError as exc:
            raise SystemExit("install imageio-ffmpeg or pass --ffmpeg") from exc
        ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    for name in args.names or CARTRIDGES:
        capture(name, args.duration, args.fps, args.warmup, ffmpeg,
                args.verbose_console)


if __name__ == "__main__":
    main()
