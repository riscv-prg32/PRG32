"""What one cartridge leaves behind must not reach the next one.

Cartridge code runs with the cartridge lock held, so firmware it calls back must
not block on that lock, and a load it requests must wait until it has returned.
Running this on a device is described in docs/software/framework_manual.md.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CART_SOURCE = (ROOT / "components" / "prg32" / "prg32_cart.c").read_text(encoding="utf-8")
BAND_SOURCE = (ROOT / "components" / "prg32" / "prg32_bands.c").read_text(encoding="utf-8")
MAIN_SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    """Return the text of a top-level C function, which ends at a column-0 brace."""
    match = re.search(rf"^[^\n;]*\b{name}\([^)]*\) ?\{{\n(.*?)^\}}", source, re.M | re.S)
    if not match:
        raise AssertionError(f"{name} not found")
    return match.group(1)


class CartridgeSwitchTest(unittest.TestCase):
    def test_lock_can_be_retaken_by_cartridge_code(self) -> None:
        self.assertIn("xSemaphoreCreateRecursiveMutex", CART_SOURCE)
        self.assertNotIn("xSemaphoreCreateMutex", CART_SOURCE)
        self.assertNotIn("xSemaphoreTake(", CART_SOURCE)
        self.assertNotIn("xSemaphoreGive(", CART_SOURCE)

    def test_selection_by_the_running_cartridge_is_deferred(self) -> None:
        body = function_body(CART_SOURCE, "prg32_cart_select_slot")
        self.assertLess(body.index("g_entry_task =="), body.index("prg32_cart_load_stored"))
        self.assertIn("g_switch_requested = true", body)

    def test_switch_loads_only_after_the_entry_returned_and_unlocked(self) -> None:
        body = function_body(CART_SOURCE, "call_entry")
        self.assertLess(body.index("entry();"), body.rindex("unlock_cart()"))
        self.assertLess(body.rindex("unlock_cart()"), body.index("if (g_switch_requested)"))

    def test_entry_pointer_is_read_with_the_lock_held(self) -> None:
        body = function_body(CART_SOURCE, "call_entry")
        self.assertLess(body.index("lock_cart()"), body.index("*entry_ptr"))

    def test_update_and_draw_wait_for_init(self) -> None:
        body = function_body(CART_SOURCE, "call_entry")
        self.assertIn("g_init_generation != g_generation", body)
        self.assertIn("call_entry(&g_init, true)", CART_SOURCE)
        self.assertIn("call_entry(&g_update, false)", CART_SOURCE)
        self.assertIn("call_entry(&g_draw, false)", CART_SOURCE)


class BandResetTest(unittest.TestCase):
    def test_reset_runs_before_cartridge_init(self) -> None:
        self.assertLess(MAIN_SOURCE.index("prg32_band_reset()"), MAIN_SOURCE.index("prg32_cart_call_init()"))

    def test_reset_restores_the_saved_choice_and_clears_cartridge_state(self) -> None:
        body = function_body(BAND_SOURCE, "prg32_band_reset")
        for expected in (
            "prg32_gfx_set_fullscreen(0)",
            "prg32_gfx_use_background_bands()",
            "g_band[band].mode = g_saved_mode[band]",
            "g_band[band].custom_bg = 0",
            "g_game_info[0]",
            "g_debug_info[0]",
        ):
            self.assertIn(expected, body)

    def test_cart_loader_does_not_take_the_graphics_lock(self) -> None:
        # The main loop takes the graphics lock and then the cartridge lock.
        self.assertNotIn("prg32_band_reset", CART_SOURCE)
        self.assertNotIn("prg32_gfx_lock", CART_SOURCE)


if __name__ == "__main__":
    unittest.main()
