import argparse
import json
from pathlib import Path
import shutil
import sys
from prg32.utilities.env_variables import ROOT_DIR, ESP32C6_BUILD_DIR
from prg32.utilities.runtime_handler import run

def prepare_legacy_esp32c6(args: argparse.Namespace) -> None:
    build_dir = ROOT_DIR / args.build_dir
    out_dir = ROOT_DIR / args.out_dir
    flasher_args = build_dir / "flasher_args.json"
    if not args.skip_build:
        run([
            "idf.py",
            "-B",
            str(build_dir.relative_to(ROOT_DIR)),
            "-D",
            f"SDKCONFIG={args.build_dir}/sdkconfig",
            "-D",
            "SDKCONFIG_DEFAULTS=sdkconfig.defaults",
            "build",
        ], cwd=ROOT_DIR)
    if not flasher_args.exists():
        raise SystemExit(f"missing {flasher_args}; run an ESP-IDF build first")

    data = json.loads(flasher_args.read_text(encoding="utf-8"))
    flash_settings = data.get("flash_settings", {})
    flash_files = data.get("flash_files", {})
    if not flash_files:
        raise SystemExit(f"{flasher_args} does not contain flash_files")

    out_dir.mkdir(parents=True, exist_ok=True)
    image = out_dir / f"{args.name}.bin"
    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        data.get("extra_esptool_args", {}).get("chip", "esp32c6"),
        "merge_bin",
        "-o",
        str(image),
        "--flash_mode",
        flash_settings.get("flash_mode", "dio"),
        "--flash_freq",
        flash_settings.get("flash_freq", "80m"),
        "--flash_size",
        flash_settings.get("flash_size", "4MB"),
    ]
    for offset, filename in sorted(flash_files.items(), key=lambda item: int(item[0], 0)):
        cmd.extend([offset, str(build_dir / filename)])
    run(cmd, cwd=ROOT_DIR)

    manifest = {
        "name": args.name,
        "target": data.get("extra_esptool_args", {}).get("chip", "esp32c6"),
        "single_file": image.name,
        "write_flash_offset": "0x0",
        "flash_settings": flash_settings,
        "source_build_dir": str(build_dir),
        "source_flash_files": flash_files,
    }
    manifest_path = out_dir / f"{args.name}.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    shutil.copy2(flasher_args, out_dir / "flasher_args.json")
    print(f"prepared {image}")
    print(f"manifest {manifest_path}")
