"""A cartridge started over the device protocol takes over from any setup screen.

Only setup_menu() handles an upload or a loaded cartridge, so every other setup
screen that waits for input must leave when prg32_setup_interrupted() is true.
Running this on a device is described in docs/software/usb_protocol.md.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components" / "prg32"

# Every setup screen with a loop that waits for input.
SETUP_SCREENS = {
    "prg32_system.c": (
        "cartridge_context_menu",
        "cartridge_picker",
        "default_cartridge_picker",
        "audio_menu",
        "developer_menu",
        "about_menu",
        "settings_menu",
    ),
    "prg32_setup_store.c": (
        "run_detail",
        "prg32_setup_store_run",
        "prg32_setup_store_browse_run",
    ),
    "prg32_wifi.c": (
        "choose_mode",
        "confirm_sta_credentials",
        "choose_ssid",
        "show_wifi_message",
        "forget_network_menu",
        "prg32_wifi_setup_run",
    ),
    "prg32_keyboard.c": ("prg32_text_input",),
}


def function_body(source: str, name: str) -> str:
    """Return one top-level C function, from its signature to its closing brace."""
    match = re.search(rf"^[a-z_].*\b{name}\(", source, re.MULTILINE)
    if match is None:
        raise AssertionError(f"function {name} not found")
    end = source.index("\n}", match.end())
    return source[match.start():end]


class SetupTakeoverTests(unittest.TestCase):
    def test_every_input_loop_of_a_setup_screen_can_be_interrupted(self) -> None:
        for file_name, functions in SETUP_SCREENS.items():
            source = (COMPONENT / file_name).read_text(encoding="utf-8")
            for name in functions:
                with self.subTest(function=name):
                    body = function_body(source, name)
                    loops = len(re.findall(r"^\s*while \(", body, re.MULTILINE))
                    checks = body.count("prg32_setup_interrupted()")
                    self.assertGreaterEqual(loops, 1, "the table lists a screen with no loop")
                    self.assertGreaterEqual(checks, 1, "a setup screen cannot be interrupted")

    def test_store_screens_check_their_nested_loops(self) -> None:
        source = (COMPONENT / "prg32_setup_store.c").read_text(encoding="utf-8")
        self.assertEqual(function_body(source, "run_detail").count("prg32_setup_interrupted()"), 2)
        self.assertEqual(function_body(source, "prg32_setup_store_run").count("prg32_setup_interrupted()"), 2)

    def test_screens_shared_with_cartridges_ignore_a_loaded_cartridge(self) -> None:
        """A cartridge may call the keyboard and Wi-Fi setup while it is loaded."""
        source = (COMPONENT / "prg32_system.c").read_text(encoding="utf-8")
        body = function_body(source, "prg32_setup_interrupted")
        self.assertIn("g_setup_open &&", body)
        self.assertEqual(source.count("  setup_menu();"), 1, "open setup through run_setup_menu()")


if __name__ == "__main__":
    unittest.main()
