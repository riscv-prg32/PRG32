import argparse
import json
import re
import unittest
from pathlib import Path
from unittest import mock

from prg32.usb_protocol import (
    FLAG_FINAL,
    HELLO,
    KEY,
    KEY_HOLD_DEFAULT_MS,
    KEY_HOLD_MAX_MS,
    KEYS,
    STATUS,
    Frame,
    FrameDecoder,
    ProtocolClient,
    encode_frame,
    key_press,
    key_release,
    key_sequence,
    normalize_keys,
    require_keys,
)

ROOT = Path(__file__).resolve().parents[1]


class FakeKeyDevice:
    """In-memory firmware model that records KEY requests."""

    def __init__(self, capabilities=("upload", "keys"), keys=KEYS) -> None:
        self.decoder = FrameDecoder()
        self.pending = bytearray()
        self.requests: list[dict] = []
        self.capabilities = list(capabilities)
        self.keys = list(keys) if keys else None

    def write(self, data: bytes) -> int:
        for frame in self.decoder.feed(data):
            if frame.message_type == HELLO:
                reply = {"ok": True, "capabilities": self.capabilities}
                if self.keys:
                    reply["keys"] = self.keys
            else:
                self.requests.append(json.loads(frame.payload))
                reply = {"ok": True, "active": self.requests[-1].get("keys", [])}
            self.pending.extend(encode_frame(Frame(STATUS, frame.request_id, json.dumps(reply).encode(), FLAG_FINAL)))
        return len(data)

    def read(self, size: int) -> bytes:
        data = bytes(self.pending[:size])
        del self.pending[:size]
        return data

    def close(self) -> None:
        pass


class KeyClientTests(unittest.TestCase):
    def test_press_sends_action_keys_and_hold(self) -> None:
        device = FakeKeyDevice()
        reply = key_press(ProtocolClient(device, timeout=1.0), ["UP", "A"], 350)
        self.assertEqual(device.requests, [{"action": "press", "hold_ms": 350, "keys": ["UP", "A"]}])
        self.assertEqual(reply["active"], ["UP", "A"])

    def test_release_without_keys_releases_all(self) -> None:
        device = FakeKeyDevice()
        key_release(ProtocolClient(device, timeout=1.0))
        self.assertEqual(device.requests, [{"action": "release", "keys": []}])

    def test_hold_time_is_checked_before_contacting_device(self) -> None:
        device = FakeKeyDevice()
        for hold_ms in (0, KEY_HOLD_MAX_MS + 1):
            with self.assertRaises(SystemExit):
                key_press(ProtocolClient(device, timeout=1.0), ["A"], hold_ms)
        self.assertEqual(device.requests, [])

    def test_sequence_presses_each_step_and_waits_between_them(self) -> None:
        device = FakeKeyDevice()
        with mock.patch("prg32.usb_protocol.time.sleep") as sleep:
            key_sequence(ProtocolClient(device, timeout=1.0), [["UP"], ["A", "B"]], 200, 100)
        self.assertEqual([r["keys"] for r in device.requests], [["UP"], ["A", "B"]])
        sleep.assert_called_once_with(0.3)

    def test_names_are_normalized_and_select_is_start(self) -> None:
        self.assertEqual(normalize_keys(["up", " Select "]), ["UP", "START"])
        with self.assertRaisesRegex(SystemExit, "unknown key 'JUMP'"):
            normalize_keys(["JUMP"])

    def test_host_uses_the_key_list_the_device_reports(self) -> None:
        device = FakeKeyDevice(keys=KEYS + ("TURBO",))
        allowed = require_keys(ProtocolClient(device, timeout=1.0))
        self.assertEqual(normalize_keys(["turbo"], allowed), ["TURBO"])

    def test_hello_without_key_list_falls_back_to_known_keys(self) -> None:
        device = FakeKeyDevice(keys=None)
        self.assertEqual(require_keys(ProtocolClient(device, timeout=1.0)), KEYS)

    def test_firmware_without_capability_is_refused(self) -> None:
        device = FakeKeyDevice(capabilities=("upload", "run"))
        with self.assertRaisesRegex(SystemExit, "does not accept key commands"):
            require_keys(ProtocolClient(device, timeout=1.0))


class KeyCliTests(unittest.TestCase):
    def _run(self, device: FakeKeyDevice, keys: list[str], release: bool = False) -> None:
        from prg32.esp32c6 import key_esp32c6
        args = argparse.Namespace(port="fake", baud="115200", timeout=1.0, keys=keys,
                                  hold_ms=KEY_HOLD_DEFAULT_MS, gap_ms=0, release=release)
        with mock.patch.object(key_esp32c6, "open_stream", return_value=device), \
             mock.patch("prg32.usb_protocol.time.sleep"), mock.patch("builtins.print"):
            key_esp32c6.key_esp32c6(args)

    def test_plus_makes_a_chord_and_arguments_make_steps(self) -> None:
        device = FakeKeyDevice()
        self._run(device, ["up", "A+B"])
        self.assertEqual([r["keys"] for r in device.requests], [["UP"], ["A", "B"]])

    def test_release_flattens_keys(self) -> None:
        device = FakeKeyDevice()
        self._run(device, ["A+B", "UP"], release=True)
        self.assertEqual(device.requests, [{"action": "release", "keys": ["A", "B", "UP"]}])

    def test_no_key_and_no_release_is_refused_before_opening_the_port(self) -> None:
        with self.assertRaisesRegex(SystemExit, "at least one key"):
            self._run(FakeKeyDevice(), [])

    def test_cli_is_registered_on_both_targets(self) -> None:
        source = (ROOT / "prg32/prg32.py").read_text(encoding="utf-8")
        self.assertIn('esp32c6_sub.add_parser("key"', source)
        self.assertIn('qemu_sub.add_parser("key"', source)


class KeyConsistencyTests(unittest.TestCase):
    """A new PRG32_BTN_* button fails here until every key list has it."""

    def setUp(self) -> None:
        self.header = (ROOT / "components/prg32/include/prg32.h").read_text(encoding="utf-8")
        self.controller = (ROOT / "components/prg32/prg32_controller.c").read_text(encoding="utf-8")
        self.protocol = (ROOT / "components/prg32/prg32_usb_protocol.c").read_text(encoding="utf-8")

    def test_firmware_table_covers_every_player_one_button(self) -> None:
        buttons = set(re.findall(r"#define PRG32_BTN_(\w+) \(1u << \d+\)", self.header))
        table = re.findall(r'\{"(\w+)", PRG32_BTN_(\w+)\}', self.controller)
        self.assertEqual(buttons, {button for _, button in table})
        self.assertEqual(set(KEYS), {name for name, _ in table})
        self.assertEqual(int(re.search(r"#define PRG32_REMOTE_KEY_COUNT (\d+)", self.header).group(1)), len(table))

    def test_message_number_and_limits_match_the_firmware(self) -> None:
        self.assertEqual(int(re.search(r"MESSAGE_KEY = (0x[0-9A-Fa-f]+)", self.protocol).group(1), 16), KEY)
        self.assertIn(f"#define PROTOCOL_KEY_HOLD_DEFAULT_MS {KEY_HOLD_DEFAULT_MS}u", self.protocol)
        self.assertIn(f"#define PROTOCOL_KEY_HOLD_MAX_MS {KEY_HOLD_MAX_MS}u", self.protocol)

    def test_remote_keys_are_not_in_the_cartridge_abi(self) -> None:
        abi = (ROOT / "prg32/abi/prg32_abi.json").read_text(encoding="utf-8")
        self.assertNotIn("prg32_controller_remote", abi)


if __name__ == "__main__":
    unittest.main()
