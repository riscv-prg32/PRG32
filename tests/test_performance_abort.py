"""A performance suite does not outlive the cartridge that began it.

prg32_perf_begin() refuses a new suite while one is running, so every path that
stops a cartridge must abort its suite, or later runs fail until a restart. The
abort must run with the cartridge lock held, because that lock is what keeps it
from freeing the sample buffer under a running update or draw call.
Running this on a device is described in docs/performance_test.md.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CART_SOURCE = ROOT / "components" / "prg32" / "prg32_cart.c"
CARTRIDGE_SOURCE = ROOT / "cartridges" / "performancetest" / "src" / "performancetest.c"


def function_body(source: str, name: str) -> str:
    """Return the text of a top-level C function, which ends at a column-0 brace."""
    match = re.search(rf"^[^\n;]*\b{name}\([^)]*\) ?\{{\n(.*?)^\}}", source, re.M | re.S)
    if not match:
        raise AssertionError(f"{name} not found")
    return match.group(1)


class PerformanceAbortTest(unittest.TestCase):
    def setUp(self) -> None:
        self.source = CART_SOURCE.read_text(encoding="utf-8")

    def test_unload_aborts_with_the_lock_held(self) -> None:
        body = function_body(self.source, "prg32_cart_unload")
        self.assertLess(body.index("lock_cart()"), body.index("prg32_perf_abort()"))
        self.assertLess(body.index("prg32_perf_abort()"), body.index("unlock_cart()"))

    def test_replacing_a_cartridge_aborts_before_its_code_is_overwritten(self) -> None:
        body = function_body(self.source, "load_image_locked")
        # A rejected image leaves the running cartridge and its suite alone.
        self.assertLess(body.index("validate_header"), body.index("prg32_perf_abort()"))
        self.assertLess(body.index("prg32_perf_abort()"), body.index("memset(prg32_cart_exec"))

    def test_only_unload_and_load_stop_a_running_cartridge(self) -> None:
        # prg32_cart_init() is the boot path, when no suite can exist yet.
        allowed = ("load_image_locked", "prg32_cart_init", "prg32_cart_unload")
        bodies = "".join(function_body(self.source, name) for name in allowed)
        self.assertEqual(
            self.source.count("g_loaded = false;"), bodies.count("g_loaded = false;")
        )


class PerformanceCartridgeTest(unittest.TestCase):
    def test_failed_start_is_not_reported_as_an_abort(self) -> None:
        source = CARTRIDGE_SOURCE.read_text(encoding="utf-8")
        body = function_body(source, "start")
        self.assertIn("g_done=DONE_NOT_STARTED", body)
        # A case that cannot begin must not leave its suite running.
        self.assertIn("if(begun)prg32_perf_abort()", body)
        self.assertIn("PERFORMANCE TEST DID NOT START", source)


if __name__ == "__main__":
    unittest.main()
