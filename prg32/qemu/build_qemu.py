#!/usr/bin/env python3

import subprocess
import argparse
from prg32.utilities.logging import *
from prg32.utilities.local_config import erase_sdkconfigs, apply_local_config_to_sdkconfig
from prg32.utilities.env_variables import QEMU_BUILD_DIR, QEMU_SDKCONFIG, QEMU_SDKCONFIG_DEFAULTS, QEMU_ELF, QEMU_IMAGE, ROOT_DIR

def get_qemu_sdkconfig_defaults(args: argparse.Namespace) -> str:
    defaults = QEMU_SDKCONFIG_DEFAULTS
    if getattr(args, 'debug', False):
        defaults += ";profiles/sdkconfig.defaults.debug"
    if (ROOT_DIR / "profiles" / "sdkconfig.defaults.local").is_file():
        defaults += ";profiles/sdkconfig.defaults.local"
    return defaults

def set_target_qemu(args: argparse.Namespace):
    step("Configuring QEMU target (esp32c3)...")
    defaults = get_qemu_sdkconfig_defaults(args)
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={defaults}", "set-target", "esp32c3"])

def build_qemu(args: argparse.Namespace):
    if not (getattr(args, 'no_erase_sdkconfig', False) or getattr(args, 'keep_sdkconfig', False)):
        erase_sdkconfigs("qemu")
    if (args.skip_target):
        log_info("Skipping setting target to ESP32C3...")
    else:
        set_target_qemu(args)

    apply_local_config_to_sdkconfig("qemu")

    step("Building QEMU...")
    defaults = get_qemu_sdkconfig_defaults(args)
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={defaults}", "build"])
    log_ok(f"Created {QEMU_BUILD_DIR} directory.")

    # Verify
    if not (Path := __import__('pathlib').Path)(QEMU_ELF).exists():
        die(f"Missing {QEMU_ELF} after build.")
    log_ok("Firmware build ready")

def flash_qemu(args: argparse.Namespace):
    step("Generating QEMU flash_image and efuse...")
    import sys
    cwd = __import__('pathlib').Path(QEMU_BUILD_DIR)
    subprocess.check_call([sys.executable, "-m", "esptool", "--chip=esp32c3", "merge_bin", "--output=qemu_flash.bin", "--fill-flash-size=4MB", "@flash_args"], cwd=str(cwd))
    if not (Path := __import__('pathlib').Path)(QEMU_IMAGE).exists():
        die(f"Missing {QEMU_IMAGE} after build.")
    log_ok(f"Created {QEMU_IMAGE}.")

    from prg32.utilities.environment_check import ensure_qemu_efuse
    ensure_qemu_efuse()

def build_and_flash_qemu(args: argparse.Namespace):
    build_qemu(args)
    flash_qemu(args)
