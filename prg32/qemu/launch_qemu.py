#!/usr/bin/env python3
import os
import sys
import shutil
import subprocess
import argparse
from pathlib import Path
from prg32.utilities.env_variables import QEMU_BUILD_DIR, QEMU_IMAGE, QEMU_EFUSE, ROOT_DIR
from prg32.utilities.logging import *

# Instance N uses these TCP ports plus 10 * N, so instance 0 is the plain emulator.
DEVICE_PROTOCOL_PORT = 5555
MONITOR_PORT = 5556
AUDIO_PORT = 4321
INSTANCE_PORT_STEP = 10
# The nvs partition in profiles/partitions_prg32.csv.
NVS_OFFSET = 0x9000
NVS_SIZE = 0x6000


def prepare_instance(instance: int) -> tuple[str, str]:
    """Return the flash and efuse images of one emulator instance.

    The first use copies the built image and erases the copy's NVS, which also
    holds the multiplayer installation identity. Every instance therefore makes
    its own identity on its first boot, as a real installation would. Later runs
    keep the instance's flash, so its cartridges and identity stay.
    """
    directory = Path(QEMU_BUILD_DIR) / "instances" / str(instance)
    directory.mkdir(parents=True, exist_ok=True)
    flash = directory / "qemu_flash.bin"
    efuse = directory / "qemu_efuse.bin"
    if not flash.exists():
        shutil.copyfile(QEMU_IMAGE, flash)
        with flash.open("r+b") as image:
            image.seek(NVS_OFFSET)
            image.write(b"\xff" * NVS_SIZE)
        log_info(f"Created QEMU instance {instance} with a new identity: {flash}")
    if not efuse.exists():
        shutil.copyfile(QEMU_EFUSE, efuse)
    return str(flash), str(efuse)


def launch_qemu(args: argparse.Namespace):
    if not Path(QEMU_IMAGE).exists():
        log_error(f"Cannot launch QEMU: {QEMU_IMAGE} image is missing.")
        die("You have to build and flash QEMU before launch.")

    instance = getattr(args, "instance", None)
    no_audio = getattr(args, "no_audio", False)
    flash_image, efuse_image = QEMU_IMAGE, QEMU_EFUSE
    port_offset = 0
    if instance is not None:
        flash_image, efuse_image = prepare_instance(instance)
        port_offset = instance * INSTANCE_PORT_STEP
    device_port = DEVICE_PROTOCOL_PORT + port_offset
    audio_port = AUDIO_PORT + port_offset

    log_info("Launching QEMU")
    log_info(f"Device protocol: socket://127.0.0.1:{device_port}")
    log_info("Press Ctrl + ] to exit")
    # Without a player nothing would connect to the audio port, so QEMU must
    # not wait for it. A background instance has no terminal for the monitor.
    audio_serial = (
        f"tcp:127.0.0.1:{audio_port},server=on,wait=off,nodelay=on"
        if no_audio
        else f"tcp::{audio_port},server,wait,nodelay"
    )
    monitor = (
        f"tcp:127.0.0.1:{MONITOR_PORT + port_offset},server=on,wait=off"
        if instance is not None
        else "stdio"
    )
    cmd = [
        "qemu-system-riscv32",
        "-M",
        "esp32c3",
        "-m",
        "4M",
        "-drive",
        f"file={flash_image},if=mtd,format=raw",
        "-drive",
        f"file={efuse_image},if=none,format=raw,id=efuse",
        "-global",
        "driver=nvram.esp32c3.efuse,property=drive,value=efuse",
        "-global",
        "driver=timer.esp32c3.timg,property=wdt_disable,value=true",
        "-nic",
        "user,model=open_eth",
        "-display",
        "sdl",
        "-monitor",
        monitor,
        "-serial",
        f"tcp:127.0.0.1:{device_port},server=on,wait=off,nodelay=on",
        "-serial",
        audio_serial,
    ]
    
    audio_player_path = ROOT_DIR / "tools" / "qemu_audio_player.py"
    audio_proc = None
    qemu_proc = None
    
    # Put terminal in cbreak mode so QEMU receives single characters instantly
    try:
        import termios
        import tty
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        tty.setcbreak(fd)
    except Exception:
        old_settings = None

    try:
        # Start the Python audio player in the background FIRST.
        # -u forces unbuffered output so the console logs appear instantly.
        if not no_audio:
            audio_proc = subprocess.Popen(
                [sys.executable, "-u", str(audio_player_path), f"socket://127.0.0.1:{audio_port}"]
            )

        # Start QEMU in the foreground (it inherits stdin/stdout automatically)
        qemu_proc = subprocess.Popen(cmd)

        # Block until QEMU exits, but monitor the audio player so we don't hang 
        # forever if the player fails to start (e.g. PyAudio missing).
        import time
        while qemu_proc.poll() is None:
            if audio_proc and audio_proc.poll() is not None:
                log_error("Audio player exited unexpectedly. Terminating QEMU.")
                qemu_proc.terminate()
                break
            time.sleep(0.1)

    except KeyboardInterrupt:
        if qemu_proc:
            qemu_proc.terminate()
    finally:
        # Guarantee the audio player is killed when QEMU dies abruptly or exits normally
        if audio_proc:
            audio_proc.terminate()
            audio_proc.wait()

        if old_settings is not None:
            try:
                termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
            except Exception:
                pass
