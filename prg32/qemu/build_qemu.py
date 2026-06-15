#!/usr/bin/env python3

import subprocess
import argparse
from prg32.utilities.logging import *
from prg32.utilities.env_variables import QEMU_BUILD_DIR, QEMU_SDKCONFIG, QEMU_SDKCONFIG_DEFAULTS, QEMU_ELF, QEMU_IMAGE

def set_target_qemu(args: argparse.Namespace):
    step("Configuring QEMU target (esp32c3)...")
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_SDKCONFIG_DEFAULTS}", "set-target", "esp32c3"])

def build_qemu(args: argparse.Namespace):
    if (args.skip_target):
        log_info("Skipping setting target to ESP32C3...")
    else:
        set_target_qemu(args)

    step("Building QEMU...")
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_SDKCONFIG_DEFAULTS}", "build"])
    log_ok(f"Created {QEMU_BUILD_DIR} directory.")

    # Verify
    if not (Path := __import__('pathlib').Path)(QEMU_ELF).exists():
        die(f"Missing {QEMU_ELF} after build.")
    log_ok("Firmware build ready")

def flash_qemu(args: argparse.Namespace):
    step("Building QEMU firmware and flash_image...")
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_SDKCONFIG_DEFAULTS}", "qemu", "--graphics", "monitor"])
    if not (Path := __import__('pathlib').Path)(QEMU_IMAGE).exists():
        die(f"Missing {QEMU_IMAGE} after build.")
    log_ok(f"Created {QEMU_IMAGE}.")

def build_and_flash_qemu(args: argparse.Namespace):
    build_qemu(args)
    flash_qemu(args)
