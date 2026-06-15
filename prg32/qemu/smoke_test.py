#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path
from prg32.utilities.logging import *
from prg32.utilities.env_variables import PRG32_ENTRY

def require_cmd(cmd, hint):
    from shutil import which
    if not which(cmd):
        log_error(f"{cmd} not found. {hint}")


def run_step(name, *cmd):
    try:
        subprocess.check_call(list(cmd))
        log_ok(name)
    except subprocess.CalledProcessError:
        log_error(name)


def main():
    ROOT = Path(__file__).resolve().parents[2]
    QEMU_BUILD_DIR = "build-qemu"
    QEMU_SDKCONFIG = f"{QEMU_BUILD_DIR}/sdkconfig"
    QEMU_DEFAULTS = "sdkconfig.defaults.qemu"
    require_cmd("python3", "Install Python 3 and retry.")
    require_cmd("idf.py", "Run: . $HOME/esp-idf/export.sh")
    # riscv toolchain optional
    run_step("doctor", sys.executable, PRG32_ENTRY, "doctor")

    run_step("set-target-esp32c3", "idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_DEFAULTS}", "set-target", "esp32c3")
    run_step("build-firmware", "idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_DEFAULTS}", "build")

    if not Path(f"{QEMU_BUILD_DIR}/PRG32.elf").exists():
        log_error("build-firmware (missing build-qemu/PRG32.elf)")
    log_ok("firmware-artifact")

    run_step("build-demo-cartridge", sys.executable, PRG32_ENTRY, "build", "examples/games/pong/graphics/game.S", "--firmware-elf", f"{QEMU_BUILD_DIR}/PRG32.elf", "--entry-prefix", "pong_graphics", "--name", "pong", "--out", f"{QEMU_BUILD_DIR}/pong.prg32")

    if not Path(f"{QEMU_BUILD_DIR}/pong.prg32").exists():
        log_error("build-demo-cartridge (missing build-qemu/pong.prg32)")
    log_ok("cartridge-artifact")

    # Try launching QEMU non-blocking to check it starts
    try:
        cmd = [
            "idf.py",
            "-B",
            QEMU_BUILD_DIR,
            "-D",
            f"SDKCONFIG={QEMU_SDKCONFIG}",
            "-D",
            f"SDKCONFIG_DEFAULTS={QEMU_DEFAULTS}",
            "qemu",
            "--graphics",
            "monitor",
        ]
        p = subprocess.Popen(cmd)
        try:
            import time
            time.sleep(8)
            if p.poll() is not None:
                raise SystemExit(f"qemu exited early with code {p.returncode}")
        finally:
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except Exception:
                    p.kill()
    except Exception:
        log_error("qemu-launch-check failed (non-blocking)")

    if not Path(f"{QEMU_BUILD_DIR}/qemu_flash.bin").exists():
        log_error("qemu-flash-image missing (run QEMU once with build-qemu/sdkconfig)")
    log_ok("qemu-flash-image")

    run_step("stage-cartridge-qemu", sys.executable, PRG32_ENTRY, "upload-qemu", f"{QEMU_BUILD_DIR}/pong.prg32", "--flash", f"{QEMU_BUILD_DIR}/qemu_flash.bin")
    log_ok("smoke-test-complete")
    print("=== SMOKE TEST PASSED ===")


if __name__ == "__main__":
    import sys
    main()
