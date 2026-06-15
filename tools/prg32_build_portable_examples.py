#!/usr/bin/env python3
"""Build all checked-in examples as portable CartridgeStore bundles."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[1]
GAME_PREFIX_OVERRIDES = {
    "space_invaders": "invaders",
    "wing_commander": "wing_commander",
}


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def entry_prefix(source: Path) -> str:
    name = source.parents[1].name if "games" in source.parts else source.parent.name
    if "games" in source.parts:
        base = GAME_PREFIX_OVERRIDES.get(name, name)
        variant = source.parent.name
        if variant == "c":
            return f"{base}_c"
        return f"{base}_{variant}"
    base = source.parent.name
    if source.parent.name == "c":
        base = source.parents[1].name
        return f"{base}_c"
    return base


def title_for(source: Path) -> str:
    if "games" in source.parts:
        return f"{source.parents[1].name}-{source.parent.name}"
    if source.parent.name == "c":
        return f"{source.parents[1].name}-c"
    return source.parent.name


def discover_examples() -> list[Path]:
    patterns = [
        "examples/games/*/ascii/game.S",
        "examples/games/*/graphics/game.S",
        "examples/games/*/c/game.c",
        "examples/features/*/demo.S",
        "examples/features/*/c/demo.c",
    ]
    examples: list[Path] = []
    for pattern in patterns:
        examples.extend(sorted(ROOT.glob(pattern)))
    return examples


def bundle_for(cartridge: Path, out_dir: Path, architecture: str, version: str) -> Path:
    stem = cartridge.stem
    manifest = {
        "abi": "prg32-metadata-1.0",
        "id": f"org.prg32.examples.{stem}",
        "title": stem,
        "version": version,
        "summary": "PRG32 portable teaching example.",
        "tags": ["example", "portable", architecture],
        "assets": {},
        "architectures": [{"id": architecture, "file": cartridge.name}],
    }
    manifest_path = out_dir / f"{stem}-{architecture}.manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    bundle = out_dir / f"{stem}-{architecture}.zip"
    with zipfile.ZipFile(bundle, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.write(manifest_path, "manifest.json")
        zf.write(cartridge, cartridge.name)
    return bundle


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", default="build-portable-examples")
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument(
        "--architecture",
        action="append",
        choices=["esp32c6", "qemu"],
        help="architecture bundle to generate; repeatable, default is both",
    )
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args(argv)

    out_dir = ROOT / args.out_dir
    if args.clean and out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    architectures = args.architecture or ["esp32c6", "qemu"]
    examples = discover_examples()
    if not examples:
        raise SystemExit("no examples found")

    rows = []
    for source in examples:
        name = title_for(source)
        cart = out_dir / f"{name}.prg32"
        run([
            sys.executable,
            "-m",
            "prg32",
            "build-cartridge",
            str(source.relative_to(ROOT)),
            "--portable",
            "--entry-prefix",
            entry_prefix(source),
            "--name",
            name,
            "--out",
            str(cart.relative_to(ROOT)),
        ])
        for architecture in architectures:
            bundle = bundle_for(cart, out_dir, architecture, args.version)
            rows.append((name, architecture, cart, bundle))

    print("\nBuilt portable examples:")
    for name, architecture, cart, bundle in rows:
        print(f"  {name:32} {architecture:7} {cart} {bundle}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
