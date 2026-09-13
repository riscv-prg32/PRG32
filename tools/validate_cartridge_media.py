#!/usr/bin/env python3
"""Validate checked-in cartridge screenshots and downloadable MP4 previews."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR":
        raise ValueError(f"{path}: not a PNG with an IHDR header")
    return struct.unpack(">II", data[16:24])


def mp4_duration(path: Path) -> float:
    data = path.read_bytes()
    if b"ftyp" not in data[:64] or b"vide" not in data or b"soun" not in data:
        raise ValueError(f"{path}: MP4 must contain video and audio tracks")
    marker = data.find(b"mvhd")
    if marker < 0:
        raise ValueError(f"{path}: missing movie header")
    version = data[marker + 4]
    if version == 0:
        timescale, duration = struct.unpack(">II", data[marker + 16:marker + 24])
    elif version == 1:
        timescale = struct.unpack(">I", data[marker + 24:marker + 28])[0]
        duration = struct.unpack(">Q", data[marker + 28:marker + 36])[0]
    else:
        raise ValueError(f"{path}: unsupported movie-header version {version}")
    return duration / timescale


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("screenshot", type=Path)
    parser.add_argument("preview", type=Path)
    args = parser.parse_args()
    width, height = png_size(args.screenshot)
    if width < 320 or height < 200:
        raise SystemExit(f"{args.screenshot}: expected at least 320x200, got {width}x{height}")
    duration = mp4_duration(args.preview)
    if not 29.5 <= duration <= 30.5:
        raise SystemExit(f"{args.preview}: expected 30 seconds, got {duration:.3f}")
    print(f"media OK: {width}x{height} screenshot, {duration:.3f}s audiovisual preview")


if __name__ == "__main__":
    main()
