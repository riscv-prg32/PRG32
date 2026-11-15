"""The L and R bumper buttons are a per-profile option, off by default."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def kconfig_entry(name):
    """Return the text of one `config <name>` entry in the PRG32 Kconfig."""
    kconfig = (ROOT / "components/prg32/Kconfig").read_text()
    match = re.search(rf"^config {name}\n(.*?)(?=^\S|\Z)", kconfig, re.M | re.S)
    if match is None:
        raise AssertionError(f"{name} is missing from components/prg32/Kconfig")
    return match.group(1)


def profile(name):
    return (ROOT / f"profiles/sdkconfig.pins.{name}").read_text()


class LrButtonsOptionTests(unittest.TestCase):
    def test_option_is_off_by_default_and_gates_the_pins(self):
        self.assertIn("default n", kconfig_entry("PRG32_ENABLE_LR_BUTTONS"))
        for pin in ("PRG32_PIN_BTN_L", "PRG32_PIN_BTN_R"):
            self.assertIn("depends on PRG32_ENABLE_LR_BUTTONS", kconfig_entry(pin))

    def test_only_breadboard2_enables_the_bumpers(self):
        text = profile("breadboard2")
        self.assertIn("CONFIG_PRG32_ENABLE_LR_BUTTONS=y", text)
        self.assertIn("CONFIG_PRG32_PIN_BTN_L=21", text)
        self.assertIn("CONFIG_PRG32_PIN_BTN_R=22", text)
        for name in ("breadboard1", "pcb1"):
            text = profile(name)
            self.assertIn("CONFIG_PRG32_ENABLE_LR_BUTTONS=n", text)
            self.assertNotIn("CONFIG_PRG32_PIN_BTN_L=", text)
            self.assertNotIn("CONFIG_PRG32_PIN_BTN_R=", text)

    def test_disabled_bumpers_have_no_gpio(self):
        config = (ROOT / "main/prg32_config.h").read_text()
        self.assertIn("#if defined(CONFIG_PRG32_ENABLE_LR_BUTTONS)", config)
        self.assertIn("#define PRG32_PIN_BTN_L -1", config)
        self.assertIn("#define PRG32_PIN_BTN_R -1", config)


if __name__ == "__main__":
    unittest.main()
