from pathlib import Path
import os
import subprocess
import sys
import shutil
from prg32.utilities.logging import *
from prg32.utilities.env_variables import *
from prg32.utilities.partition_handler import read_partition_slot
import argparse

def validate_project_layout():
    if not Path(QEMU_BUILD_DIR).exists():
        log_info(f"{QEMU_BUILD_DIR} is missing; creating it")
        Path(QEMU_BUILD_DIR).mkdir(parents=True, exist_ok=True)
    if not Path(ESP32C6_BUILD_DIR).exists():
        log_info(f"{ESP32C6_BUILD_DIR} is missing; creating it")
        Path(ESP32C6_BUILD_DIR).mkdir(parents=True, exist_ok=True)
    log_ok("Project layout looks valid")
    
def ensure_qemu_flash():
    step(f"Ensuring QEMU flash image exists ({QEMU_IMAGE})")
    p = Path(QEMU_BUILD_DIR)
    p.mkdir(parents=True, exist_ok=True)
    qemu_image = Path(QEMU_IMAGE)
    if not qemu_image.exists():
        log_info("Creating 4MB QEMU flash image")
        qemu_image.write_bytes(b"\x00" * FLASH_SIZE)
    size = qemu_image.stat().st_size
    if size != FLASH_SIZE:
        die(f"{QEMU_IMAGE} has size {size} bytes, expected {FLASH_SIZE} bytes (4MB).")
    log_info("QEMU flash image ready (4MB)")

def ensure_qemu_efuse():
    step(f"Ensuring QEMU efuse image exists ({QEMU_EFUSE})")
    out_path = Path(QEMU_EFUSE)
    if not out_path.exists():
        # replicate small Python snippet from original
        import importlib.util
        qemu_ext_path = Path(os.environ.get("IDF_PATH", "")) / "tools" / "idf_py_actions" / "qemu_ext.py"
        if not qemu_ext_path.exists():
            die(f"qemu_ext.py not found at {qemu_ext_path}")
        spec = importlib.util.spec_from_file_location("qemu_ext", str(qemu_ext_path))
        module = importlib.util.module_from_spec(spec)
        assert spec and spec.loader
        spec.loader.exec_module(module)
        out_path.write_bytes(module.QEMU_TARGETS["esp32c3"].default_efuse)
    log_info("QEMU efuse image ready")


def ensure_qemu_firmware():
    if Path(QEMU_ELF).exists():
        log_ok(f"QEMU firmware already built ({QEMU_ELF})")
        return
    log_info("QEMU firmware missing; running initial build")
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_SDKCONFIG_DEFAULTS}", "set-target", "esp32c3"])
    subprocess.check_call(["idf.py", "-B", QEMU_BUILD_DIR, "-D", f"SDKCONFIG={QEMU_SDKCONFIG}", "-D", f"SDKCONFIG_DEFAULTS={QEMU_SDKCONFIG_DEFAULTS}", "build"])
    if not Path(QEMU_ELF).exists():
        die(f"Build finished but {QEMU_ELF} is still missing.")
    log_ok("QEMU firmware build complete")


def check_qemu_runtime_deps():
    try:
        out = subprocess.run(["qemu-system-riscv32", "-machine", "help"], capture_output=True, text=True)
    except FileNotFoundError as e:
        die(f"qemu-system-riscv32 not found: {e}")
    if out.returncode != 0:
        if "libslirp" in out.stderr or "libslirp" in out.stdout:
            log_error("qemu-system-riscv32 failed due to missing libslirp.")
            print("Run:\nbrew install libslirp")
            sys.exit(1)
        die(f"qemu-system-riscv32 failed to start: {out.stderr or out.stdout}")
    log_ok("qemu-system-riscv32 runtime check passed")


def find_idf_root() -> Path:
    step("Checking ESP-IDF path")
    candidates = []
    idf_path = os.environ.get("IDF_PATH")
    if idf_path and (Path(idf_path) / "export.sh").exists():
        return Path(idf_path)

    candidates = [Path.home() / "esp-idf", Path.home() / "Desktop" / "esp-idf"]
    for c in candidates:
        if (c / "export.sh").exists():
            return c
    raise FileNotFoundError("ESP-IDF root not found")

def ensure_idf_tools() -> bool:
    return shutil.which("idf.py") is not None and shutil.which("riscv32-esp-elf-gcc") is not None


def load_idf_env():
    if ensure_idf_tools():
        log_ok("ESP-IDF already loaded")
        return
    try:
        idf_root = find_idf_root()
    except FileNotFoundError:
        log_error("ESP-IDF not loaded and no local checkout was found.")
        print("Run one of these commands after installing ESP-IDF:\n. ~/esp-idf/export.sh\n. ~/Desktop/esp-idf/export.sh")
        sys.exit(1)
    log_info(f"Loading ESP-IDF from {idf_root}")
    # Shell source not possible; attempt to exec export.sh in a bash subprocess and capture vars
    cmd = ["bash", "-c", f"source {idf_root / 'export.sh'} && env"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    for line in p.stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            os.environ.setdefault(k, v)
    import shutil as _sh
    if not (_sh.which("idf.py") and _sh.which("riscv32-esp-elf-gcc")):
        die("ESP-IDF export loaded, but essential tools are still unavailable.")
    log_ok("ESP-IDF environment loaded")
    check_qemu_runtime_deps()


def check_python():
    import shutil
    if not shutil.which("python3"):
        die("python3 not found. Install Python 3 first (for example: brew install python).")
    log_ok("python3 detected")

def doctor(args: argparse.Namespace) -> None:
    failures = 0

    if args.host_only:
        log_ok("host-only mode skips ESP-IDF toolchain checks")
    else:
        if ensure_idf_tools():
            log_ok("idf.py found")
        else:
            failures += 1
            log_error("idf.py missing (you must source ESP-IDF export.sh)")

        gcc_name = args.tool_prefix + "gcc"
        if shutil.which(gcc_name):
            log_ok(f"{gcc_name} found")
        else:
            failures += 1
            log_error(f"{gcc_name} missing (source ESP-IDF export.sh)")

    partitions_path = Path(args.partitions)
    if partitions_path.exists():
        log_ok(f"partitions file found: {partitions_path}")
        try:
            offset, size = read_partition_slot(partitions_path, args.slot)
        except SystemExit as exc:
            failures += 1
            log_error(str(exc))
        else:
            log_ok(
                f"partition {args.slot} parsed "
                f"(offset=0x{offset:06x}, size={size})"
            )
    else:
        failures += 1
        log_error(f"partitions file missing: {partitions_path}")

    if failures:
        raise SystemExit(1)