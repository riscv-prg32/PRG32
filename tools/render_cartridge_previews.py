#!/usr/bin/env python3
"""Render 30-second cartridge MP4 previews with original synthesized audio."""

from __future__ import annotations

import argparse
import math
import subprocess
import tempfile
import wave
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance


ROOT = Path(__file__).resolve().parents[1]
FPS = 12
DURATION = 30
RATE = 44_100
CARTRIDGES = {
    "bachdemo": ("cartridges/bachdemo/assets/screenshot.png", (48, 52, 55, 60, 64)),
    "blackjack": ("cartridges/blackjack/screenshot.png", (45, 52, 57, 60, 64)),
    "devicedemo": ("cartridges/devicedemo/assets/screenshot.png", (48, 55, 60, 67, 72)),
    "poing": ("cartridges/poing/assets/screenshot.png", (40, 47, 52, 59, 64)),
}


def write_audio(path: Path, notes: tuple[int, ...], seed: int) -> None:
    phase_l = phase_r = 0.0
    frames = bytearray()
    for sample in range(RATE * DURATION):
        beat = sample // (RATE // 4)
        midi = notes[(beat + seed) % len(notes)] + (12 if (beat // len(notes)) & 1 else 0)
        frequency = 440.0 * (2.0 ** ((midi - 69) / 12.0))
        phase_l += frequency / RATE
        phase_r += frequency * (1.003 + seed * 0.0003) / RATE
        envelope = min(1.0, (sample % (RATE // 4)) / 800.0) * 0.16
        left = int(32767 * envelope * (2.0 * (phase_l % 1.0) - 1.0))
        right = int(32767 * envelope * math.sin(phase_r * math.tau))
        frames.extend(left.to_bytes(2, "little", signed=True))
        frames.extend(right.to_bytes(2, "little", signed=True))
    with wave.open(str(path), "wb") as wav:
        wav.setparams((2, 2, RATE, RATE * DURATION, "NONE", "not compressed"))
        wav.writeframes(frames)


def render(name: str, ffmpeg: str) -> None:
    source_name, notes = CARTRIDGES[name]
    source_path = ROOT / source_name
    output = source_path.parent / "preview.mp4"
    base = Image.open(source_path).convert("RGB").resize((640, 400), Image.Resampling.LANCZOS)
    with tempfile.TemporaryDirectory(prefix="prg32-preview-") as temp:
        audio = Path(temp) / "audio.wav"
        write_audio(audio, notes, list(CARTRIDGES).index(name))
        command = [
            ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
            "-s", "640x400", "-r", str(FPS), "-i", "-", "-i", str(audio),
            "-c:v", "libx264", "-preset", "medium", "-crf", "25", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "96k", "-t", str(DURATION), "-movflags", "+faststart",
            str(output),
        ]
        process = subprocess.Popen(command, stdin=subprocess.PIPE)
        assert process.stdin is not None
        for frame in range(FPS * DURATION):
            pulse = 1.0 + 0.025 * math.sin(frame * math.tau / FPS / 4)
            image = ImageEnhance.Brightness(base).enhance(pulse)
            draw = ImageDraw.Draw(image)
            x = int(640 * frame / (FPS * DURATION - 1))
            draw.rectangle((0, 394, x, 399), fill=(38, 224, 200))
            process.stdin.write(image.tobytes())
        process.stdin.close()
        if process.wait() != 0:
            raise SystemExit(f"ffmpeg failed while rendering {name}")
    print(f"wrote {output.relative_to(ROOT)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("names", nargs="*", choices=CARTRIDGES)
    parser.add_argument("--ffmpeg", help="ffmpeg executable; defaults to imageio-ffmpeg's binary")
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
        render(name, ffmpeg)


if __name__ == "__main__":
    main()
