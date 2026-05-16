#!/usr/bin/env python3
"""Convert images, GIF frames, sprites, and tiles into PRG32 C or assembly."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def require_pillow():
    try:
        from PIL import Image, ImageSequence
    except ImportError as exc:
        raise SystemExit("Pillow is required: python3 -m pip install pillow") from exc
    return Image, ImageSequence


def rgb565(pixel: tuple[int, int, int]) -> int:
    r, g, b = pixel[:3]
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def emit_c(symbol: str, values: list[int], width: int, height: int) -> str:
    lines = [
        "#include <stdint.h>",
        f"#define {symbol.upper()}_W {width}",
        f"#define {symbol.upper()}_H {height}",
        f"const uint16_t {symbol}[] = {{",
    ]
    for i in range(0, len(values), 8):
        chunk = ", ".join(f"0x{v:04x}" for v in values[i:i + 8])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def emit_asm(symbol: str, values: list[int], width: int, height: int) -> str:
    lines = [
        f".equ {symbol.upper()}_W, {width}",
        f".equ {symbol.upper()}_H, {height}",
        ".section .rodata",
        f".global {symbol}",
        f"{symbol}:",
    ]
    for i in range(0, len(values), 8):
        chunk = ", ".join(f"0x{v:04x}" for v in values[i:i + 8])
        lines.append(f"    .half {chunk}")
    return "\n".join(lines) + "\n"


def load_frames(path: Path, width: int | None, height: int | None, crop: str | None):
    Image, ImageSequence = require_pillow()
    image = Image.open(path)
    frames = []
    box = None
    if crop:
        parts = [int(p) for p in crop.split(",")]
        if len(parts) != 4:
            raise SystemExit("--crop expects x,y,w,h")
        x, y, w, h = parts
        box = (x, y, x + w, y + h)

    for frame in ImageSequence.Iterator(image):
        current = frame.convert("RGB")
        if box:
            current = current.crop(box)
        if width and height:
            current = current.resize((width, height), Image.Resampling.NEAREST)
        frames.append(current)
    return frames


def convert(args: argparse.Namespace) -> None:
    frames = load_frames(args.input, args.width, args.height, args.crop)
    if args.frames:
        frames = frames[:args.frames]
    if not frames:
        raise SystemExit("no frames found")

    width, height = frames[0].size
    values: list[int] = []
    for frame in frames:
        if frame.size != (width, height):
            raise SystemExit("all frames must have the same size")
        values.extend(rgb565(pixel) for pixel in frame.getdata())

    text = emit_c(args.symbol, values, width, height)
    if args.format == "asm":
        text = emit_asm(args.symbol, values, width, height)

    if args.out:
        args.out.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--symbol", default="asset")
    parser.add_argument("--format", choices=["c", "asm"], default="c")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--crop", help="crop rectangle x,y,w,h before resizing")
    parser.add_argument("--frames", type=int, help="maximum frames from animated input")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    if (args.width is None) != (args.height is None):
        raise SystemExit("--width and --height must be used together")
    convert(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
