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



class ServerAddressTest(unittest.TestCase):
    """No server address is built in: the user sets one, or mDNS finds it."""

    def test_no_default_address_in_the_firmware(self) -> None:
        self.assertIn('#define PRG32_MULTIPLAYER_SERVER_URL ""', SOURCE)
        kconfig = (COMPONENT / "Kconfig").read_text(encoding="utf-8")
        entry = kconfig[kconfig.index("config PRG32_MULTIPLAYER_SERVER_URL"):]
        entry = entry[:entry.index("\nconfig ")]
        self.assertIn('default ""', entry)
        for name in ("prg32_multiplayer.c", "prg32_setup_multiplayer.c"):
            text = (COMPONENT / name).read_text(encoding="utf-8")
            self.assertNotRegex(text, r'"wss?://[0-9]', name)

    def test_saved_address_comes_first(self) -> None:
        body = SOURCE[SOURCE.index("int prg32_multiplayer_server_url_configured("):]
        body = body[:body.index("\n}")]
        self.assertLess(body.index("prg32_multiplayer_server_url_get"),
                        body.index("PRG32_MULTIPLAYER_SERVER_URL"))

    def test_join_without_an_address_waits_for_discovery(self) -> None:
        join = SOURCE[SOURCE.rindex("int prg32_multiplayer_join("):]
        self.assertIn("client == 1 && PRG32_MP_DISCOVERY_ENABLE", join)

    def test_discovery_asks_mdns_for_the_service(self) -> None:
        task = SOURCE[SOURCE.index("static void discovery_task"):]
        self.assertIn("prg32_multiplayer_discover_mdns(url, sizeof(url))", task)
        wire = (COMPONENT / "prg32_multiplayer_wire.h").read_text(encoding="utf-8")
        self.assertIn('#define PRG32_MP_MDNS_SERVICE "_prg32mp"', wire)

    def test_public_server_is_the_last_resort(self) -> None:
        kconfig = (COMPONENT / "Kconfig").read_text(encoding="utf-8")
        entry = kconfig[kconfig.index("config PRG32_MULTIPLAYER_PUBLIC_SERVER_URL"):]
        entry = entry[:entry.index("\nconfig ")]
        # The hosted server, reached directly until its proxy forwards WebSockets.
        self.assertIn('default "ws://193.205.230.7:8099"', entry)
        task = SOURCE[SOURCE.index("static void discovery_task"):]
        fallback = task.index("prg32_multiplayer_server_url_public(url, sizeof(url))")
        # Only with no address known, and only after local discovery had its time.
        self.assertIn("if (g_server_url[0] == '\\0')", task[:fallback])
        self.assertIn("PRG32_MP_PUBLIC_FALLBACK_MS", task[:fallback])
        self.assertIn("#define PRG32_MP_PUBLIC_FALLBACK_MS 10000", SOURCE)
        configured = SOURCE[SOURCE.index("int prg32_multiplayer_server_url_configured("):]
        configured = configured[:configured.index("\n}")]
        self.assertNotIn("PUBLIC", configured)

    def test_a_saved_address_is_used_alone(self) -> None:
        needed = SOURCE[SOURCE.index("static bool discovery_needed_locked(void)"):]
        self.assertIn("!g_saved_url_in_use", needed[:needed.index("\n}\n")])
        client = SOURCE[SOURCE.index("static int ensure_client(void)"):]
        client = client[:client.index("\n}\n")]
        self.assertIn("g_saved_url_in_use =\n            prg32_multiplayer_server_url_get(", client)
        # The client is chosen before discovery starts, so the task sees the flag.
        join = SOURCE[SOURCE.rindex("int prg32_multiplayer_join("):]
        self.assertLess(join.index("int client = ensure_client();"), join.index("start_discovery();"))

    def test_discovery_task_runs_only_while_needed(self) -> None:
        body = SOURCE[SOURCE.index("static void discovery_task"):SOURCE.index("static void start_discovery(void) {")]
        self.assertIn("if (discovery_finish()) {\n                break;", body)
        self.assertIn("vTaskDelete(NULL);", body)
        self.assertNotIn("for (;;) {\n        if (!discovery_wanted()) {", body)
        # A lost connection asks for a task again.
        self.assertIn("schedule_reconnect(data->client);\n            /* Does nothing unless a cartridge "
                      "still wants a room. */\n            start_discovery();", SOURCE)

    def test_a_search_shows_the_server_as_local(self) -> None:
        switch = SOURCE[SOURCE.index("static void switch_server"):]
        switch = switch[:switch.index("\n}\n")]
        self.assertIn("g_server_is_local = local;", switch)
        self.assertIn('switch_server(url, "public fallback", false);', SOURCE)
        self.assertIn('switch_server(url, "mDNS", true);', SOURCE)
        screen = (COMPONENT / "prg32_setup_multiplayer.c").read_text(encoding="utf-8")
        self.assertIn('"FOUND: LOCAL"', screen)
        self.assertIn("prg32_multiplayer_server_is_local()", screen)

    def test_the_network_coming_up_starts_a_search(self) -> None:
        wifi = (COMPONENT / "prg32_wifi.c").read_text(encoding="utf-8")
        got_ip = wifi[wifi.index("id == IP_EVENT_STA_GOT_IP) {"):]
        self.assertIn("prg32_multiplayer_network_up();", got_ip[:got_ip.index("\n    }\n")])
        up = SOURCE[SOURCE.index("void prg32_multiplayer_network_up(void) {\n    /*"):]
        up = up[:up.index("\n}\n")]
        self.assertIn("if (!automatic_discovery_mode()) {\n        return;", up)
        self.assertIn("!g_mp.member_requested", up)
        self.assertIn("xTaskCreate(network_up_search_task", up)
        task = SOURCE[SOURCE.index("static void network_up_search_task"):]
        task = task[:task.index("\n}\n")]
        self.assertIn("room_requested()", task)
        self.assertIn("vTaskDelete(NULL);", task)

    def test_setup_menu_offers_the_server_screen(self) -> None:
        self.assertIn('"MULTIPLAYER SETUP"', SYSTEM_SOURCE)
        self.assertIn("prg32_setup_multiplayer_run();", SYSTEM_SOURCE)


if __name__ == "__main__":
    unittest.main()
