"""Check the multiplayer wire code against prg32-multiplayer-1.0 on a computer.

The C test beside this file links the firmware's own prg32_multiplayer_wire.c
and ESP-IDF's cJSON. It is skipped when no C compiler or ESP-IDF is available.
Running the protocol against a server is tools/multiplayer_wall_test.py.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components" / "prg32"
SOURCE = (COMPONENT / "prg32_multiplayer.c").read_text(encoding="utf-8")
CART_SOURCE = (COMPONENT / "prg32_cart.c").read_text(encoding="utf-8")
SYSTEM_SOURCE = (COMPONENT / "prg32_system.c").read_text(encoding="utf-8")


def cjson_directory() -> Path | None:
    idf = os.environ.get("IDF_PATH") or str(Path.home() / "esp-idf")
    candidate = Path(idf) / "components" / "json" / "cJSON"
    return candidate if (candidate / "cJSON.c").exists() else None


class MultiplayerWireTest(unittest.TestCase):
    def test_wire_code_on_host(self) -> None:
        compiler = shutil.which("cc")
        cjson = cjson_directory()
        if not compiler or not cjson:
            self.skipTest("needs a C compiler and ESP-IDF's cJSON")
        with tempfile.TemporaryDirectory() as directory:
            program = Path(directory) / "wire"
            subprocess.check_call(
                [
                    compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    f"-I{COMPONENT / 'include'}", f"-I{COMPONENT}", f"-I{cjson}",
                    str(ROOT / "tests" / "multiplayer_wire_test.c"),
                    str(COMPONENT / "prg32_multiplayer_wire.c"),
                    str(cjson / "cJSON.c"),
                    "-o", str(program),
                ]
            )
            subprocess.check_call([str(program)])


class MultiplayerLifecycleTest(unittest.TestCase):
    """Rules of the transport that the host test cannot reach."""

    def test_local_id_is_zero_until_welcome(self) -> None:
        self.assertIn("g_mp.link == PRG32_MP_LINK_JOINED ? g_mp.player_id : 0", SOURCE)

    def test_state_is_sent_only_after_welcome(self) -> None:
        tick = SOURCE[SOURCE.rindex("void prg32_multiplayer_tick(void)"):]
        send = tick.index("prg32_mp_format_state")
        self.assertIn("g_mp.link == PRG32_MP_LINK_JOINED", tick[:send])

    def test_a_new_socket_starts_a_new_session(self) -> None:
        self.assertIn("case WEBSOCKET_EVENT_CONNECTED:\n            forget_session(", SOURCE)
        self.assertIn("forget_session(PRG32_MP_LINK_DISCONNECTED);\n            schedule_reconnect", SOURCE)

    def test_only_text_frames_are_parsed(self) -> None:
        self.assertIn("data->op_code == 0x01", SOURCE)

    def test_local_identity_is_never_logged(self) -> None:
        for line in SOURCE.splitlines():
            if "ESP_LOG" in line:
                self.assertNotIn("g_local_device_id", line)

    def test_qemu_keeps_the_ethernet_interrupt_masked(self) -> None:
        body = SOURCE[SOURCE.index("static void ethernet_poll_task"):]
        self.assertIn("REG_WRITE(PRG32_OPENETH_INT_MASK_REG, 0);", body)
        self.assertIn("xTaskNotifyGive(rx_task);", body)

    def test_wifi_driver_does_not_log_the_mac(self) -> None:
        wifi = (COMPONENT / "prg32_wifi.c").read_text(encoding="utf-8")
        self.assertIn('esp_log_level_set("wifi", ESP_LOG_WARN);', wifi)

    def test_a_session_does_not_outlive_its_cartridge(self) -> None:
        self.assertEqual(CART_SOURCE.count("prg32_multiplayer_leave();"), 2)
        self.assertIn("prg32_multiplayer_leave();", SYSTEM_SOURCE)


if __name__ == "__main__":
    unittest.main()
