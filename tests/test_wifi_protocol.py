"""Host-side tests for the device-protocol Wi-Fi messages (dummy values only)."""

import argparse
import json
import re
import unittest
from pathlib import Path
from unittest import mock

from prg32.usb_protocol import (
    HELLO,
    STATUS,
    WIFI_FORGET,
    WIFI_SCAN,
    WIFI_SET,
    Frame,
    FrameDecoder,
    ProtocolClient,
    encode_frame,
    require_wifi,
    wifi_forget,
    wifi_scan,
    wifi_set,
)

ROOT = Path(__file__).resolve().parents[1]


class FakeWifiDevice:
    """Answers HELLO and the Wi-Fi messages like the firmware does."""

    def __init__(self, capabilities=("upload", "run", "performance", "wifi")) -> None:
        self.decoder = FrameDecoder()
        self.pending = bytearray()
        self.capabilities = list(capabilities)
        self.requests: list[tuple[int, dict]] = []
        self.known = ["LabNet"]

    def _reply(self, request: Frame, value: dict) -> None:
        self.pending.extend(encode_frame(Frame(STATUS, request.request_id, json.dumps(value).encode())))

    def write(self, data: bytes) -> int:
        for frame in self.decoder.feed(data):
            body = json.loads(frame.payload or b"{}")
            self.requests.append((frame.message_type, body))
            if frame.message_type == HELLO:
                self._reply(frame, {"ok": True, "capabilities": self.capabilities})
            elif frame.message_type == WIFI_SCAN:
                self._reply(frame, {
                    "ok": True,
                    "networks": [{"ssid": "LabNet", "rssi": -50, "channel": 6, "auth": "WPA2", "known": True}],
                    "known": self.known, "connected": "LabNet", "ip": "10.0.0.5", "truncated": False,
                })
            elif frame.message_type == WIFI_SET:
                if len(body.get("password", "")) < 8:
                    self._reply(frame, {"ok": False, "error": "AUTH FAILED"})
                else:
                    self.known.insert(0, body["ssid"])
                    self._reply(frame, {"ok": True, "connected": body["ssid"], "ip": "10.0.0.6", "known": self.known})
            elif frame.message_type == WIFI_FORGET:
                if body["ssid"] in self.known:
                    self.known.remove(body["ssid"])
                    self._reply(frame, {"ok": True, "known": self.known, "connected": "", "ip": ""})
                else:
                    self._reply(frame, {"ok": False, "error": "network is not stored"})
        return len(data)

    def read(self, size: int) -> bytes:
        data = bytes(self.pending[:size])
        del self.pending[:size]
        return data

    def close(self) -> None:
        pass


class WifiProtocolTests(unittest.TestCase):
    def test_scan_returns_networks_and_stored_names(self) -> None:
        reply = wifi_scan(ProtocolClient(FakeWifiDevice(), timeout=1.0))
        self.assertEqual(reply["networks"][0]["ssid"], "LabNet")
        self.assertEqual(reply["known"], ["LabNet"])

    def test_set_sends_credentials_and_reports_failure(self) -> None:
        device = FakeWifiDevice()
        client = ProtocolClient(device, timeout=1.0)
        reply = wifi_set(client, "HomeNet", "example-pass")
        self.assertEqual(reply["connected"], "HomeNet")
        self.assertEqual(device.requests[-1], (WIFI_SET, {"ssid": "HomeNet", "password": "example-pass"}))
        with self.assertRaisesRegex(SystemExit, "AUTH FAILED"):
            wifi_set(client, "HomeNet", "short")

    def test_forget_removes_a_stored_network_only(self) -> None:
        client = ProtocolClient(FakeWifiDevice(), timeout=1.0)
        self.assertEqual(wifi_forget(client, "LabNet")["known"], [])
        with self.assertRaisesRegex(SystemExit, "not stored"):
            wifi_forget(client, "LabNet")

    def test_hosts_check_the_wifi_capability_first(self) -> None:
        client = ProtocolClient(FakeWifiDevice(capabilities=("upload", "run")), timeout=1.0)
        with self.assertRaisesRegex(SystemExit, "does not offer Wi-Fi"):
            require_wifi(client)

    def test_message_numbers_match_the_firmware(self) -> None:
        source = (ROOT / "components/prg32/prg32_usb_protocol.c").read_text(encoding="utf-8")
        for name, value in (("WIFI_SCAN", WIFI_SCAN), ("WIFI_SET", WIFI_SET), ("WIFI_FORGET", WIFI_FORGET)):
            match = re.search(rf"MESSAGE_{name} = (0x[0-9A-Fa-f]+)", source)
            self.assertIsNotNone(match, name)
            self.assertEqual(int(match.group(1), 16), value, name)

    def test_cli_never_takes_a_password_argument(self) -> None:
        # A password on the command line would end up in shell history.
        source = (ROOT / "prg32/prg32.py").read_text(encoding="utf-8")
        wifi_section = source.split('"wifi-set"', 1)[1].split("set_defaults", 1)[0]
        self.assertNotIn('"--password"', wifi_section)
        self.assertIn("--password-stdin", wifi_section)

    def test_wifi_set_reads_password_from_stdin(self) -> None:
        from prg32.esp32c6 import wifi_esp32c6
        device = FakeWifiDevice()
        args = argparse.Namespace(port="fake", baud="115200", timeout=1.0, ssid="HomeNet", password_stdin=True)
        with mock.patch.object(wifi_esp32c6, "open_stream", return_value=device), \
             mock.patch("sys.stdin.readline", return_value="example-pass\n"), \
             mock.patch("builtins.print"):
            wifi_esp32c6.wifi_set_esp32c6(args)
        self.assertEqual(device.requests[-1][1]["password"], "example-pass")


class WifiFirmwareSourceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.wifi = (ROOT / "components/prg32/prg32_wifi.c").read_text(encoding="utf-8")
        self.header = (ROOT / "components/prg32/include/prg32.h").read_text(encoding="utf-8")

    def test_keeps_the_last_four_networks(self) -> None:
        self.assertIn("#define PRG32_WIFI_KNOWN_MAX 4", self.header)
        self.assertIn('nvs_set_blob(nvs, "known"', self.wifi)

    def test_forgetting_also_clears_the_driver_copy(self) -> None:
        forget = self.wifi.split("int prg32_wifi_forget_network(const char *ssid) {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("known_remove(index);", forget)
        self.assertIn("esp_wifi_restore();", forget)

    def test_network_management_is_not_in_the_cartridge_abi(self) -> None:
        abi = (ROOT / "prg32/abi/prg32_abi.json").read_text(encoding="utf-8")
        for name in ("prg32_wifi_scan_networks", "prg32_wifi_known_networks",
                     "prg32_wifi_connect_network", "prg32_wifi_forget_network"):
            self.assertNotIn(name, abi)


if __name__ == "__main__":
    unittest.main()
