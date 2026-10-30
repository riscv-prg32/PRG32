"""Tests for finding a connected ESP32-C6 by its native USB ID."""

import argparse
import unittest
from types import SimpleNamespace
from unittest import mock

from prg32.esp32c6 import find_port
from prg32.esp32c6.find_port import (
    ESPRESSIF_USB_JTAG_PID,
    ESPRESSIF_USB_VID,
    find_esp32c6_ports,
    resolve_port,
)


def serial_port(device, vid=None, pid=None, serial_number=None, description="n/a"):
    """Shape of a pyserial ListPortInfo entry, with only the fields used."""
    return SimpleNamespace(device=device, vid=vid, pid=pid,
                           serial_number=serial_number, description=description)


ESP32C6_MAC = serial_port("/dev/cu.usbmodem101", ESPRESSIF_USB_VID, ESPRESSIF_USB_JTAG_PID, "F0:F5:BD:01:02:03")
ESP32C6_WINDOWS = serial_port("COM5", ESPRESSIF_USB_VID, ESPRESSIF_USB_JTAG_PID)
CP2102_BRIDGE = serial_port("/dev/cu.usbserial-0001", 0x10C4, 0xEA60, description="CP2102 USB to UART")
CH343_BRIDGE = serial_port("/dev/ttyACM1", 0x1A86, 0x55D3, description="USB Single Serial")
BLUETOOTH = serial_port("/dev/cu.Bluetooth-Incoming-Port")


def connected(*ports):
    return mock.patch("serial.tools.list_ports.comports", return_value=list(ports))


class FindPortTests(unittest.TestCase):
    def test_only_the_native_usb_port_matches(self) -> None:
        # The UART bridge connector does not run the device protocol.
        with connected(BLUETOOTH, CP2102_BRIDGE, CH343_BRIDGE, ESP32C6_MAC):
            self.assertEqual([p.device for p in find_esp32c6_ports()], ["/dev/cu.usbmodem101"])

    def test_one_board_is_chosen_automatically(self) -> None:
        with connected(BLUETOOTH, ESP32C6_WINDOWS), mock.patch("builtins.print"):
            self.assertEqual(resolve_port(None), "COM5")

    def test_an_explicit_port_is_never_replaced(self) -> None:
        with connected(ESP32C6_MAC):
            self.assertEqual(resolve_port("/dev/ttyACM0"), "/dev/ttyACM0")
            self.assertEqual(resolve_port("socket://127.0.0.1:5555"), "socket://127.0.0.1:5555")

    def test_no_board_explains_the_usual_causes(self) -> None:
        with connected(BLUETOOTH, CP2102_BRIDGE):
            with self.assertRaises(SystemExit) as raised:
                resolve_port(None)
        message = str(raised.exception)
        self.assertIn("labelled USB, not UART", message)
        self.assertIn("dialout", message)
        self.assertIn("--port", message)

    def test_several_boards_are_listed_instead_of_guessed(self) -> None:
        # Guessing could overwrite a cartridge slot on someone else's board.
        with connected(ESP32C6_WINDOWS, ESP32C6_MAC):
            with self.assertRaises(SystemExit) as raised:
                resolve_port(None)
        message = str(raised.exception)
        self.assertIn("2 ESP32-C6 boards", message)
        self.assertIn("COM5", message)
        self.assertIn("/dev/cu.usbmodem101 (serial number F0:F5:BD:01:02:03)", message)

    def test_upload_without_url_or_port_uses_the_found_board(self) -> None:
        from prg32.esp32c6 import upload_esp32c6
        args = argparse.Namespace(cartridge="game.prg32", url=None, port=None)
        with connected(ESP32C6_MAC), mock.patch("builtins.print"), \
             mock.patch.object(upload_esp32c6.Path, "read_bytes", return_value=b"cart"), \
             mock.patch.object(upload_esp32c6, "ensure_cart_max_size"), \
             mock.patch.object(upload_esp32c6, "upload_esp32c6_usb") as usb_upload:
            upload_esp32c6.upload_esp32c6(args)
        self.assertEqual(usb_upload.call_args.args[0].port, "/dev/cu.usbmodem101")

    def test_cli_accepts_commands_without_port(self) -> None:
        from prg32 import prg32
        parsed = []
        original_parse_args = argparse.ArgumentParser.parse_args

        def parse_without_running(parser, argv=None, namespace=None):
            # Keep the parsed arguments and replace the command with a no-op.
            args = original_parse_args(parser, argv, namespace)
            parsed.append(args)
            args.func = lambda _args: None
            return args

        with mock.patch.object(argparse.ArgumentParser, "parse_args", parse_without_running):
            for argv in (["esp32c6", "upload", "game.prg32"], ["esp32c6", "run"],
                         ["esp32c6", "performance"],
                         ["esp32c6", "key", "A"], ["esp32c6", "wifi-scan"],
                         ["esp32c6", "wifi-forget", "--ssid", "LabNet"]):
                with self.subTest(argv=argv):
                    self.assertEqual(prg32.main(argv), 0)
                    self.assertIsNone(parsed[-1].port)
            prg32.main(["qemu", "key", "A"])
            self.assertEqual(parsed[-1].port, "socket://127.0.0.1:5555")


class ListPortsTests(unittest.TestCase):
    def test_ports_command_separates_boards_from_other_ports(self) -> None:
        printed = []
        with connected(CP2102_BRIDGE, ESP32C6_MAC), \
             mock.patch("builtins.print", side_effect=lambda *a, **k: printed.append(" ".join(map(str, a)))):
            find_port.list_ports_esp32c6(argparse.Namespace())
        text = "\n".join(printed)
        self.assertIn("/dev/cu.usbmodem101", text.split("Other serial ports")[0])
        self.assertIn("/dev/cu.usbserial-0001", text.split("Other serial ports")[1])


if __name__ == "__main__":
    unittest.main()
