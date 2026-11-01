#!/usr/bin/env python3
"""QEMU smoke test: build the firmware, boot it, and talk to it.

The test needs no terminal, so it also runs from scripts and agent shells.
QEMU is started directly (not through ``idf.py qemu monitor``, whose monitor
refuses to start without a terminal) with an SDL display, because the virtual
display blocks the firmware when no display backend runs. The test then checks
that the firmware answers HELLO on the device protocol and runs a cartridge
uploaded through it. Every failed step stops the test with exit code 1.
"""
import socket
import subprocess
import sys
import time
from pathlib import Path

# The script is started as a file, so make the repository importable.
ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from prg32.utilities.logging import log_ok, step, die
from prg32.utilities.env_variables import QEMU_BUILD_DIR, QEMU_IMAGE, QEMU_EFUSE
from prg32.utilities.environment_check import enable_ccache
from prg32.usb_protocol import ProtocolClient, open_stream, hello, upload_cartridge

CARTRIDGE = f"{QEMU_BUILD_DIR}/pong.prg32"
CARTRIDGE_SLOT = "cart0"
PROTOCOL_HOST = "127.0.0.1"
PROTOCOL_PORT = 5555
QEMU_LOG = f"{QEMU_BUILD_DIR}/smoke_qemu.log"
BOOT_TIMEOUT_S = 60.0


def require_cmd(cmd, hint):
    from shutil import which
    if not which(cmd):
        die(f"{cmd} not found. {hint}")


def run_step(name, *cmd):
    """Run one command; a failure stops the whole smoke test."""
    try:
        subprocess.check_call(list(cmd))
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        die(f"{name} failed: {exc}")
    log_ok(name)


def protocol_port_is_free():
    """Return True when nothing is listening on the device protocol port."""
    try:
        with socket.create_connection((PROTOCOL_HOST, PROTOCOL_PORT), timeout=0.5):
            return False
    except OSError:
        return True


def start_qemu():
    """Start QEMU without a monitor, its log written to QEMU_LOG."""
    cmd = [
        "qemu-system-riscv32",
        "-M", "esp32c3",
        "-m", "4M",
        "-drive", f"file={QEMU_IMAGE},if=mtd,format=raw",
        "-drive", f"file={QEMU_EFUSE},if=none,format=raw,id=efuse",
        "-global", "driver=nvram.esp32c3.efuse,property=drive,value=efuse",
        "-global", "driver=timer.esp32c3.timg,property=wdt_disable,value=true",
        "-nic", "user,model=open_eth",
        "-display", "sdl",
        "-monitor", "none",
        "-serial", f"tcp:{PROTOCOL_HOST}:{PROTOCOL_PORT},server=on,wait=off,nodelay=on",
    ]
    log_file = open(QEMU_LOG, "wb")
    return subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=log_file, stderr=subprocess.STDOUT)


def stop_qemu(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def wait_for_hello(process):
    """Connect to the device protocol and return the first HELLO reply.

    The firmware answers only once it has booted, so connection and HELLO are
    retried until BOOT_TIMEOUT_S has passed.
    """
    deadline = time.monotonic() + BOOT_TIMEOUT_S
    last_error = "no answer"
    while time.monotonic() < deadline:
        if process.poll() is not None:
            die(f"QEMU exited with code {process.returncode} during boot; see {QEMU_LOG}")
        client = None
        try:
            client = ProtocolClient(open_stream(f"socket://{PROTOCOL_HOST}:{PROTOCOL_PORT}", 115200, 2.0), timeout=3.0)
            return client, hello(client)
        except (OSError, SystemExit) as exc:
            last_error = str(exc)
            if client is not None:
                client.close()
            time.sleep(1.0)
    die(f"firmware did not answer HELLO within {BOOT_TIMEOUT_S:.0f} s ({last_error}); see {QEMU_LOG}")


def check_running_firmware():
    """Boot QEMU, check HELLO, run the demo cartridge over the protocol."""
    if not protocol_port_is_free():
        die(f"port {PROTOCOL_PORT} is already in use; close the running QEMU first")
    process = start_qemu()
    client = None
    try:
        client, info = wait_for_hello(process)
        if info.get("protocol") != "prg32-io-1.0":
            die(f"unexpected protocol in HELLO: {info.get('protocol')!r}")
        log_ok(f"qemu-hello (target {info.get('target')}, firmware {info.get('firmware')})")

        data = Path(CARTRIDGE).read_bytes()
        reply = upload_cartridge(client, CARTRIDGE, data, CARTRIDGE_SLOT, "run")
        if not reply.get("running"):
            die(f"uploaded cartridge is not running: {reply}")
        time.sleep(2.0)
        if process.poll() is not None:
            die(f"QEMU exited with code {process.returncode} while the cartridge ran; see {QEMU_LOG}")
        log_ok(f"qemu-upload-run ({reply.get('name')} in {reply.get('slot')})")
    finally:
        if client is not None:
            client.close()
        stop_qemu(process)


def main():
    import os
    os.chdir(ROOT)
    enable_ccache()
    require_cmd("idf.py", "Run: . $HOME/esp-idf/export.sh")
    require_cmd("qemu-system-riscv32", "Install the ESP-IDF QEMU tools: python $IDF_PATH/tools/idf_tools.py install qemu-riscv32")
    run_step("doctor", sys.executable, "-m", "prg32", "doctor")

    # The same build as `prg32 qemu build`: the full defaults (including the
    # cartridge partition table), a fresh flash image and the eFuse file.
    run_step("build-firmware", sys.executable, "-m", "prg32", "qemu", "build")
    if not Path(f"{QEMU_BUILD_DIR}/PRG32.elf").exists():
        die("build-firmware (missing build-qemu/PRG32.elf)")
    log_ok("firmware-artifact")

    run_step("build-demo-cartridge", sys.executable, "-m", "prg32", "cartridge", "build", "examples/games/pong/graphics/game.S", "--portable", "--entry-prefix", "pong_graphics", "--name", "pong", "--out", CARTRIDGE)
    if not Path(CARTRIDGE).exists():
        die(f"build-demo-cartridge (missing {CARTRIDGE})")
    log_ok("cartridge-artifact")

    run_step("stage-cartridge-qemu", sys.executable, "-m", "prg32", "qemu", "upload", CARTRIDGE, "--flash", QEMU_IMAGE)

    step("Booting QEMU and checking the device protocol")
    check_running_firmware()
    log_ok("smoke-test-complete")
    print("=== SMOKE TEST PASSED ===")


if __name__ == "__main__":
    main()
