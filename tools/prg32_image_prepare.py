#!/usr/bin/env python3
"""Interactive terminal helper for preparing PRG32 image assets."""

from __future__ import annotations

import argparse
from pathlib import Path

import prg32_image_convert


def ask(prompt: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value or default


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    args = parser.parse_args(argv)

    Image, _ = prg32_image_convert.require_pillow()
    image = Image.open(args.input)
    print(f"Input: {args.input}")
    print(f"Size: {image.size[0]}x{image.size[1]} mode={image.mode}")
    print("Target types: background, tiles, sprite, animated-sprite")

    kind = ask("Asset type", "sprite")
    symbol = ask("Symbol", args.input.stem.replace("-", "_"))
    fmt = ask("Output format (c/asm)", "c")
    width = ask("Width, blank to keep", "")
    height = ask("Height, blank to keep", "")
    crop = ask("Crop x,y,w,h, blank for none", "")
    frames = ask("Max animation frames, blank for all", "")
    suffix = "c" if fmt == "c" else "S"
    out = ask("Output path", f"build/{symbol}_{kind}.{suffix}")

    convert_args = argparse.Namespace(
        input=args.input,
        symbol=symbol,
        format=fmt,
        width=int(width) if width else None,
        height=int(height) if height else None,
        crop=crop or None,
        frames=int(frames) if frames else None,
        out=Path(out),
    )
    if (convert_args.width is None) != (convert_args.height is None):
        raise SystemExit("width and height must be provided together")
    convert_args.out.parent.mkdir(parents=True, exist_ok=True)
    prg32_image_convert.convert(convert_args)
    print(f"Wrote {convert_args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
