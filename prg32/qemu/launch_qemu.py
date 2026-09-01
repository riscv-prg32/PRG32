#!/usr/bin/env python3
import os
import sys
import subprocess
import argparse
from pathlib import Path
from prg32.utilities.env_variables import QEMU_IMAGE, QEMU_EFUSE, ROOT_DIR
from prg32.utilities.logging import *

def launch_qemu(args: argparse.Namespace):
    if not (Path := __import__('pathlib').Path)(QEMU_IMAGE).exists():
        log_error(f"Cannot launch QEMU: {QEMU_IMAGE} image is missing.")
        die("You have to build and flash QEMU before launch.")

    log_info("Launching QEMU")
    log_info("Press Ctrl + ] to exit")
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
        "-serial",
        "tcp::4321,server,wait,nodelay",
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
        audio_proc = subprocess.Popen([sys.executable, "-u", str(audio_player_path)])

        # Start QEMU in the foreground (it inherits stdin/stdout automatically)
        qemu_proc = subprocess.Popen(cmd)

        # Block until QEMU exits, but monitor the audio player so we don't hang 
        # forever if the player fails to start (e.g. PyAudio missing).
        import time
        while qemu_proc.poll() is None:
            if audio_proc.poll() is not None:
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
