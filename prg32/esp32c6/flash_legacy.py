import argparse
import json
from pathlib import Path
import sys
from prg32.utilities.runtime_handler import run

def flash_legacy_esp32c6(args: argparse.Namespace) -> None:
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
    run(cmd)
