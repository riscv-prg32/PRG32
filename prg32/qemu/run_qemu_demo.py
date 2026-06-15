#!/usr/bin/env python3
import subprocess
from pathlib import Path
from prg32.utilities.logging import *
from prg32.utilities.env_variables import PRG32_ENTRY


def main():
    QEMU_BUILD_DIR = "build-qemu"
    QEMU_SDKCONFIG = f"{QEMU_BUILD_DIR}/sdkconfig"
    QEMU_DEFAULTS = "sdkconfig.defaults.qemu"
    DEMO_SOURCE = "examples/games/asteroids/graphics/game.S"
    DEMO_PREFIX = "asteroids_graphics"
    DEMO_CART = f"{QEMU_BUILD_DIR}/asteroids.prg32"
    DEMO_FLASH = f"{QEMU_BUILD_DIR}/qemu_flash.bin"

    def require_cmd(cmd, hint):
        from shutil import which
        if not which(cmd):
            die(f"{cmd} not found. {hint}")

    require_cmd("idf.py", "Run: . $HOME/esp-idf/export.sh")
    require_cmd("python3", "Install Python 3 and retry.")
    require_cmd("riscv32-esp-elf-gcc", "Run: . $HOME/esp-idf/export.sh")

    if not __import__('os').environ.get('IDF_PATH'):
        die("ESP-IDF is not sourced (IDF_PATH is empty). Run: . $HOME/esp-idf/export.sh")

    import os
    os.chdir(Path(__file__).resolve().parents[2])

    log_info("Configuring QEMU target (esp32c3)")
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_DEFAULTS}", "set-target", "esp32c3"])

    log_info("Building QEMU firmware")
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_DEFAULTS}", "build"])
    if not Path(f"{QEMU_BUILD_DIR}/PRG32.elf").exists():
        die(f"Missing {QEMU_BUILD_DIR}/PRG32.elf after build")

    log_info("Building demo cartridge")
    subprocess.check_call(["python3", PRG32_ENTRY, "build", DEMO_SOURCE, "--firmware-elf", f"{QEMU_BUILD_DIR}/PRG32.elf", "--entry-prefix", DEMO_PREFIX, "--name", "asteroids", "--out", DEMO_CART])

    if not Path(DEMO_FLASH).exists():
        die(f"Missing {DEMO_FLASH}. Run QEMU once first with tools/qemu.sh.")

    log_info("Staging demo cartridge into QEMU flash")
    subprocess.check_call(["python3", PRG32_ENTRY, "upload-qemu", DEMO_CART, "--flash", DEMO_FLASH])

    log_info("Starting QEMU screen")
    os.execvp("idf.py", ["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_DEFAULTS}", "qemu", "--graphics", "monitor"])


if __name__ == "__main__":
    main()
