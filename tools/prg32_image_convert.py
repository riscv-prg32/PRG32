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


def pack_indexed(indices: list[int], bits_per_pixel: int) -> list[int]:
    """Pack palette indices most-significant pixel first in each byte."""
    if bits_per_pixel not in (1, 2, 4, 8):
        raise ValueError("bits_per_pixel must be 1, 2, 4, or 8")
    if bits_per_pixel == 8:
        return list(indices)
    per_byte = 8 // bits_per_pixel
    mask = (1 << bits_per_pixel) - 1
    packed = []
    for start in range(0, len(indices), per_byte):
        value = 0
        for offset, index in enumerate(indices[start:start + per_byte]):
            value |= (index & mask) << (8 - bits_per_pixel * (offset + 1))
        packed.append(value)
    return packed


def pack_bitplanes(indices: list[int], bits_per_pixel: int) -> list[int]:
    """Store one MSB-first bitmap per value bit, least-significant plane first."""
    if bits_per_pixel < 1 or bits_per_pixel > 8:
        raise ValueError("bits_per_pixel must be between 1 and 8")
    packed = []
    for plane in range(bits_per_pixel):
        bits = [(index >> plane) & 1 for index in indices]
        packed.extend(pack_indexed(bits, 1))
    return packed


def pack_bitplane_rows(indices: list[int], width: int, height: int,
                       bits_per_pixel: int) -> list[int]:
    """Pack frame-major planes with every source row byte-aligned."""
    if bits_per_pixel < 1 or bits_per_pixel > 8:
        raise ValueError("bits_per_pixel must be between 1 and 8")
    if len(indices) != width * height:
        raise ValueError("bitplane frame dimensions do not match pixel count")
    packed: list[int] = []
    for plane in range(bits_per_pixel):
        for row in range(height):
            start = row * width
            bits = [(index >> plane) & 1
                    for index in indices[start:start + width]]
            packed.extend(pack_indexed(bits, 1))
    return packed


def exact_palette_indices(values: list[int], maximum_colors: int) -> tuple[list[int], list[int]]:
    """Build a stable first-seen RGB565 palette, refusing lossy quantization."""
    palette: list[int] = []
    color_to_index: dict[int, int] = {}
    indices: list[int] = []
    for color in values:
        index = color_to_index.get(color)
        if index is None:
            if len(palette) >= maximum_colors:
                raise SystemExit(
                    f"image has more than {maximum_colors} exact RGB565 colors; "
                    "choose a larger indexed mode or reduce colors explicitly"
                )
            index = len(palette)
            color_to_index[color] = index
            palette.append(color)
        indices.append(index)
    return palette, indices


def emit_compact_c(symbol: str, data: list[int], palette: list[int], width: int,
                   height: int, frames: int, bits_per_pixel: int,
                   transparent_index: int, planar: bool = False) -> str:
    pointer_macro = "PRG32_SPRITE_BITPLANES" if planar else "PRG32_SPRITE_INDEXED"
    lines = [
        '#include "prg32.h"',
        f"#define {symbol.upper()}_W {width}",
        f"#define {symbol.upper()}_H {height}",
        f"#define {symbol.upper()}_FRAMES {frames}",
        f"#define {symbol.upper()}_SPRITE {pointer_macro}(&{symbol})",
        f"static const uint8_t {symbol}_pixels[] = {{",
    ]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{v:02x}" for v in data[i:i + 16]) + ",")
    lines.extend(["};", f"static const uint16_t {symbol}_palette[] = {{"])
    for i in range(0, len(palette), 8):
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in palette[i:i + 8]) + ",")
    lines.extend([
        "};",
        f"const prg32_indexed_sprite_t {symbol} = {{",
        f"    .pixels = {symbol}_pixels,",
        f"    .palette = {symbol}_palette,",
        f"    .width = {width},",
        f"    .height = {height},",
        f"    .frame_count = {frames},",
        f"    .palette_count = {len(palette)},",
        f"    .bits_per_pixel = {bits_per_pixel},",
        f"    .transparent_index = {transparent_index},",
        "};",
    ])
    return "\n".join(lines) + "\n"


def emit_compact_asm(symbol: str, data: list[int], palette: list[int], width: int,
                     height: int, frames: int, bits_per_pixel: int,
                     transparent_index: int, planar: bool = False) -> str:
    pointer_tag = 3 if planar else 1
    lines = [
        f".equ {symbol.upper()}_W, {width}",
        f".equ {symbol.upper()}_H, {height}",
        f".equ {symbol.upper()}_FRAMES, {frames}",
        f".equ {symbol.upper()}_SPRITE, {symbol} + {pointer_tag}",
        ".section .rodata",
        ".balign 2",
        f"{symbol}_pixels:",
    ]
    for i in range(0, len(data), 16):
        lines.append("    .byte " + ", ".join(f"0x{v:02x}" for v in data[i:i + 16]))
    lines.extend([".balign 2", f"{symbol}_palette:"])
    for i in range(0, len(palette), 8):
        lines.append("    .half " + ", ".join(f"0x{v:04x}" for v in palette[i:i + 8]))
    lines.extend([
        ".balign 4",
        f".global {symbol}",
        f"{symbol}:",
        f"    .word {symbol}_pixels",
        f"    .word {symbol}_palette",
        f"    .half {width}, {height}, {frames}",
        f"    .half {len(palette)}",
        f"    .byte {bits_per_pixel}, 0",
        f"    .half {transparent_index & 0xffff}",
    ])
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

    if args.mode == "rgb565":
        text = emit_c(args.symbol, values, width, height)
        if args.format == "asm":
            text = emit_asm(args.symbol, values, width, height)
    else:
        palette, indices = exact_palette_indices(values, args.colors)
        bits_per_pixel = {2: 1, 4: 2, 8: 3, 16: 4, 256: 8}[args.colors]
        pixels_per_frame = width * height
        data: list[int] = []
        for start in range(0, len(indices), pixels_per_frame):
            frame_indices = indices[start:start + pixels_per_frame]
            if args.mode == "indexed":
                data.extend(pack_indexed(frame_indices, bits_per_pixel))
            else:
                data.extend(pack_bitplane_rows(frame_indices, width, height,
                                               bits_per_pixel))
        emitter = emit_compact_c if args.format == "c" else emit_compact_asm
        emitter_args = (args.symbol, data, palette, width, height, len(frames),
                        bits_per_pixel, args.transparent_index)
        text = emitter(*emitter_args, planar=args.mode == "bitplanes")

    if args.out:
        args.out.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--symbol", default="asset")
    parser.add_argument("--format", choices=["c", "asm"], default="c")
    parser.add_argument("--mode", choices=["rgb565", "indexed", "bitplanes"],
                        default="rgb565")
    parser.add_argument("--colors", type=int, choices=[2, 4, 8, 16, 256], default=16,
                        help="palette size for indexed and bitplane modes")
    parser.add_argument("--transparent-index", type=int, default=-1,
                        help="palette index to skip, or -1 for opaque")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--crop", help="crop rectangle x,y,w,h before resizing")
    parser.add_argument("--frames", type=int, help="maximum frames from animated input")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    if (args.width is None) != (args.height is None):
        raise SystemExit("--width and --height must be used together")
    if args.transparent_index >= args.colors or args.transparent_index < -1:
        raise SystemExit("--transparent-index must be -1 or less than --colors")
    if args.mode == "indexed" and args.colors == 8:
        raise SystemExit("packed indexed mode supports 2, 4, 16, or 256 colors")
    convert(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
