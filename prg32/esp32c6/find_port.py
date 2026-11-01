"""Find the serial port of a connected ESP32-C6 running PRG32.

The device protocol runs on the ESP32-C6 native USB Serial/JTAG port
(GPIO12/GPIO13). Every ESP32-C6 reports the same USB vendor and product ID on
that port, so it can be found by ID on Windows, Linux, and macOS without
opening any port. Opening a port could restart the board (see the
modem-control rules in docs/software/usb_protocol.md); listing ports cannot.

A USB-to-UART bridge (the DevKitC-1 connector labelled UART) reports its chip
maker's ID instead, so it is never chosen: the protocol does not run there.
"""

from __future__ import annotations

import argparse

from prg32.utilities.logging import *

# Espressif USB Serial/JTAG, the same filter the browser clients use.
ESPRESSIF_USB_VID = 0x303A
ESPRESSIF_USB_JTAG_PID = 0x1001

NO_BOARD_HELP = """no ESP32-C6 found on a native USB port. Check that:
  - the cable is in the board connector labelled USB, not UART;
  - the cable carries data (some USB-C cables only charge);
  - no other program (a serial monitor, idf.py monitor) is using the board;
  - the board does not use the pcb1 pin profile, whose DOWN button on GPIO13
    disables the native USB port;
  - on Linux, you are in the dialout group (log out and back in after adding).
Run `python3 -m prg32 esp32c6 ports` to see every serial port, or pass --port."""


def find_esp32c6_ports() -> list:
    """Return the pyserial port entries of every ESP32-C6 native USB port."""
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit("finding the board requires pyserial; install the PRG32 package dependencies") from exc
    boards = [
        port for port in list_ports.comports()
        if port.vid == ESPRESSIF_USB_VID and port.pid == ESPRESSIF_USB_JTAG_PID
    ]
    return sorted(boards, key=lambda port: port.device)


def describe_port(port) -> str:
    """One line naming a port and, when known, the board's serial number."""
    if port.serial_number:
        return f"{port.device} (serial number {port.serial_number})"
    return port.device


def resolve_port(port: str | None) -> str:
    """Return the port to use: the one given, or the only connected ESP32-C6.

    Several boards stop the command instead of choosing one, because an upload
    to the wrong board would overwrite one of its cartridge slots.
    """
    if port:
        return port
    boards = find_esp32c6_ports()
    if not boards:
        raise SystemExit(NO_BOARD_HELP)
    if len(boards) > 1:
        listed = "\n".join("  " + describe_port(board) for board in boards)
        raise SystemExit(
            f"{len(boards)} ESP32-C6 boards are connected; choose one with --port:\n{listed}"
        )
    log_info(f"Using ESP32-C6 on {boards[0].device}")
    return boards[0].device


def list_ports_esp32c6(args: argparse.Namespace) -> None:
    """Print the connected ESP32-C6 boards and the other serial ports."""
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit("listing ports requires pyserial; install the PRG32 package dependencies") from exc
    boards = find_esp32c6_ports()
    board_devices = {board.device for board in boards}
    others = sorted(
        (port for port in list_ports.comports() if port.device not in board_devices),
        key=lambda port: port.device,
    )
    if boards:
        log_ok(f"ESP32-C6 native USB ports ({len(boards)}):")
        for board in boards:
            print("  " + describe_port(board))
    else:
        log_info("No ESP32-C6 native USB port found")
    if others:
        print("Other serial ports (not usable for cartridge uploads):")
        for port in others:
            print(f"  {port.device}  {port.description}")
