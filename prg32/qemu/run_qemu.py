#!/usr/bin/env python3
from pathlib import Path
import subprocess
import os
import sys
from prg32.utilities.env_variables import BUILD_DIR, SDKCONFIG, SDKCONFIG_DEFAULTS, QEMU_IMAGE, QEMU_EFUSE
from prg32.utilities.logging import *


def ensure_idf_env():
    step("Checking ESP-IDF environment")
    if not os.environ.get("IDF_PATH"):
        die("IDF_PATH is not set. Source ESP-IDF first (for example: . $HOME/esp-idf/export.sh).")


def ensure_qemu_flash():
    step(f"Ensuring QEMU flash image exists ({QEMU_IMAGE})")
    p = Path(BUILD_DIR)
    p.mkdir(parents=True, exist_ok=True)
    flash = Path(QEMU_IMAGE)
    if not flash.exists():
        print("Creating 4MB QEMU flash image")
        flash.write_bytes(b"\x00" * (4 * 1024 * 1024))
    size = flash.stat().st_size
    if size != 4 * 1024 * 1024:
        die(f"{flash} has size {size} bytes, expected 4MB")
    log_ok("QEMU flash image ready (4MB)")


def ensure_qemu_efuse():
    step(f"Ensuring QEMU efuse image exists ({QEMU_EFUSE})")
    if not Path(QEMU_EFUSE).exists():
        # Reuse environment_check implementation by invoking it
        from prg32.utilities.environment_check import ensure_qemu_efuse as _ensure

        _ensure()
    log_ok("QEMU efuse image ready")


def build_qemu_firmware():
    step("Configuring QEMU target (esp32c3)")
    subprocess.check_call(["idf.py", "-B", BUILD_DIR, "-D", f"SDKCONFIG={SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={SDKCONFIG_DEFAULTS}", "set-target", "esp32c3"])
    step("Building QEMU firmware")
    subprocess.check_call(["idf.py", "-B", BUILD_DIR, "-D", f"SDKCONFIG={SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={SDKCONFIG_DEFAULTS}", "build"])
    if not Path(f"{BUILD_DIR}/PRG32.elf").exists():
        die(f"Missing {BUILD_DIR}/PRG32.elf after build.")
    log_ok("Firmware build ready")


def generate_flash_image():
    step("Generating QEMU flash image")
    cwd = Path(BUILD_DIR)
    subprocess.check_call([sys.executable, "-m", "esptool", "--chip=esp32c3", "merge_bin", "--output=qemu_flash.bin", "--fill-flash-size=4MB", "@flash_args"], cwd=str(cwd))


def run_qemu():
    step("Starting QEMU")
    print("\nPress Ctrl + ] to exit QEMU monitor.\n")
    cmd = [
        "qemu-system-riscv32",
        "-M",
        "esp32c3",
        "-m",
        "4M",
        "-drive",
        f"file={QEMU_IMAGE},if=mtd,format=raw",
        "-drive",
        f"file={QEMU_EFUSE},if=none,format=raw,id=efuse",
        "-global",
        "driver=nvram.esp32c3.efuse,property=drive,value=efuse",
        "-global",
        "driver=timer.esp32c3.timg,property=wdt_disable,value=true",
        "-nic",
        "user,model=open_eth",
        "-display",
        "sdl",
        "-serial",
        "mon:stdio",
    ]
    os.execvp(cmd[0], cmd)


def main(argv=None):
    import prg32.utilities.environment_check as envcheck
    envcheck.check_python()
    ensure_idf_env()
    build_qemu_firmware()
    ensure_qemu_flash()
    ensure_qemu_efuse()
    generate_flash_image()
    run_qemu()


if __name__ == "__main__":
    main()
