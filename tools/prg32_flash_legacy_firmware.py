#!/usr/bin/env python3
"""Flash a published single-file legacy PRG32 firmware image."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", help="JSON produced by prg32_prepare_legacy_firmware.py")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", default="460800")
    args = parser.parse_args(argv)

    manifest_path = Path(args.manifest)
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    image = manifest_path.with_name(data["single_file"])
    if not image.exists():
        raise SystemExit(f"missing firmware image: {image}")
    settings = data.get("flash_settings", {})
    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        data.get("target", "esp32c6"),
        "--port",
        args.port,
        "--baud",
        args.baud,
        "write_flash",
        "--flash_mode",
        settings.get("flash_mode", "dio"),
        "--flash_freq",
        settings.get("flash_freq", "80m"),
        "--flash_size",
        settings.get("flash_size", "4MB"),
        data.get("write_flash_offset", "0x0"),
        str(image),
    ]
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
