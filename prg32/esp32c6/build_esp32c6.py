#!/usr/bin/env python3

import subprocess
import argparse
from prg32.utilities.logging import *
from prg32.utilities.env_variables import ESP32C6_BUILD_DIR, ESP32C6_SDKCONFIG, ESP32C6_SDKCONFIG_DEFAULTS, ESP32C6_ELF

def set_target_esp32c6(args: argparse.Namespace):
    step("Configuring ESP32C6 target (esp32c6)...")
    subprocess.check_call(["idf.py", "-B", ESP32C6_BUILD_DIR, "-D", f"SDKCONFIG={ESP32C6_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={ESP32C6_SDKCONFIG_DEFAULTS}", "set-target", "esp32c6"])

def build_esp32c6(args: argparse.Namespace):
    if args.skip_target:
        log_info("Skipping setting target to ESP32C6...")
    else:
        set_target_esp32c6(args)

    step("Building ESP32C6...")
    subprocess.check_call(["idf.py", "-B", ESP32C6_BUILD_DIR, "-D", f"SDKCONFIG={ESP32C6_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={ESP32C6_SDKCONFIG_DEFAULTS}", "build"])
    log_ok(f"Created {ESP32C6_BUILD_DIR} directory.")

    # Verify
    if not (Path := __import__('pathlib').Path)(ESP32C6_ELF).exists():
        die(f"Missing {ESP32C6_ELF} after build.")
    log_ok("Firmware build ready")

def flash_esp32c6(args: argparse.Namespace):
    step("Flashing the ESP32C6 firmware and flash_image via USB")
    subprocess.check_call(["idf.py", "-B", ESP32C6_BUILD_DIR, "-D", f"SDKCONFIG={ESP32C6_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={ESP32C6_SDKCONFIG_DEFAULTS}", "flash", "monitor"])

def build_and_flash_esp32c6(args: argparse.Namespace):
    build_esp32c6(args)
    flash_esp32c6(args)
    log_ok("Completed ESP32C6 build and flash")

def erase_flash_esp32c6(args: argparse.Namespace):
    step("Erasing ESP32C6 flash...")
    subprocess.check_call(["idf.py", "erase-flash"])
    log_ok("Done erasing ESP32C6 flash")

def reset_esp32c6(args: argparse.Namespace):
    erase_flash_esp32c6(args)
    flash_esp32c6(args)

