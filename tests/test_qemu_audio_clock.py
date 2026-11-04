"""The QEMU audio task must never keep a cartridge from starting.

prg32_audio_cartridge_start() takes render_lock_ before a cartridge's init. In
firmware c53c491 the audio task held that lock while it waited for the host
audio player's credit byte, with no timeout. Without a player (`qemu run
--no-audio`) the credit never came; with a player that had fallen behind, the
task took the lock again at once. Either way every `run` froze the main loop.
The whole-Store record in docs/cartridge_store/cartridge_store.md shows the
check on QEMU.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "components" / "prg32_audio"


def function_body(source: str, name: str) -> str:
    """Return one top-level C function, from its signature to its closing brace."""
    match = re.search(rf"^[a-z_].*\b{name}\(", source, re.MULTILINE)
    if match is None:
        raise AssertionError(f"function {name} not found")
    end = source.index("\n}", match.end())
    return source[match.start():end]


class QemuAudioClockTests(unittest.TestCase):
    def setUp(self) -> None:
        self.audio = (AUDIO / "audio.c").read_text(encoding="utf-8")
        self.uart = (AUDIO / "prg32_qemu_audio.c").read_text(encoding="utf-8")

    def test_credit_wait_is_outside_the_render_lock(self) -> None:
        task = function_body(self.audio, "audio_task")
        for held in re.findall(r"xSemaphoreTake\(render_lock_.*?xSemaphoreGive\(render_lock_\)",
                               task, re.DOTALL):
            self.assertNotIn("wait_for_host_credit", held)
            self.assertNotIn("render_buffer", held)
            self.assertNotIn("read_ack", held)

    def test_mixing_under_the_lock_does_not_wait_for_the_host(self) -> None:
        mix = function_body(self.audio, "mix_and_send_buffer")
        self.assertNotIn("read_ack", mix)
        self.assertNotIn("wait_for_host_credit", mix)

    def test_credit_read_has_a_timeout(self) -> None:
        read = function_body(self.uart, "prg32_qemu_audio_read_ack")
        self.assertNotIn("portMAX_DELAY", read)
        self.assertIn("timeout_ms", read)

    def test_missing_player_stops_the_wait(self) -> None:
        wait = function_body(self.audio, "wait_for_host_credit")
        self.assertIn("PRG32_QEMU_AUDIO_HOST_WAIT_MS", wait)
        self.assertIn("host_absent_ = true", wait)
        self.assertIn("host_absent_ = false", wait)

    def test_late_period_restarts_instead_of_rendering_back_to_back(self) -> None:
        task = function_body(self.audio, "audio_task")
        self.assertRegex(task, r"xTaskDelayUntil\(&next_wake[^;]*\) == pdFALSE\)\s*\{\s*next_wake = xTaskGetTickCount\(\);")


    def test_shutdown_waits_for_the_task_to_exit(self) -> None:
        """A capped wait let the old task outlive the shutdown and run next to the new one."""
        shutdown = function_body(self.audio, "prg32_audio_shutdown")
        task = function_body(self.audio, "audio_task")
        self.assertIn("xSemaphoreTake(task_exited_, portMAX_DELAY)", shutdown)
        self.assertNotIn("vTaskDelay", shutdown)
        self.assertLess(shutdown.index("task_exited_"), shutdown.index("vSemaphoreDelete"))
        self.assertLess(shutdown.index("task_exited_"), shutdown.index("memset"))
        # The task gives the semaphore as its last access to the shared state.
        tail = task[task.index("xSemaphoreGive(task_exited_)"):]
        self.assertNotIn("g_prg32_audio", tail)
        self.assertIn("vTaskDelete(NULL)", tail)


if __name__ == "__main__":
    unittest.main()
