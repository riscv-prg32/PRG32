import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class SharedRgbLedTests(unittest.TestCase):
    def test_breadboard1_has_no_shared_rgb_led(self):
        # LCD D/C is on GPIO10, so the DevKitC-1 WS2812 on GPIO8 is not shared.
        profile = (ROOT / "profiles/sdkconfig.pins.breadboard1").read_text()
        self.assertIn("CONFIG_PRG32_PIN_LCD_DC=10", profile)
        self.assertIn("CONFIG_PRG32_PIN_RGB_LED=-1", profile)
        self.assertIn("CONFIG_PRG32_PIN_SHARED_RGB_LED=-1", profile)

    def test_pcb1_shares_the_rgb_led_with_lcd_dc(self):
        profile = (ROOT / "profiles/sdkconfig.pins.pcb1").read_text()
        self.assertIn("CONFIG_PRG32_PIN_LCD_DC=8", profile)
        self.assertIn("CONFIG_PRG32_PIN_RGB_LED=-1", profile)
        self.assertIn("CONFIG_PRG32_PIN_SHARED_RGB_LED=8", profile)

    def test_shared_led_is_boot_only_and_relatched_after_lcd_updates(self):
        system = (ROOT / "components/prg32/prg32_system.c").read_text()
        display = (ROOT / "components/prg32/prg32_display_ili9341.c").read_text()
        led = (ROOT / "components/prg32/prg32_led.c").read_text()
        self.assertIn("prg32_shared_rgb_led_boot_flash(PRG32_PIN_SHARED_RGB_LED)", system)
        self.assertIn("prg32_shared_rgb_led_off(PRG32_PIN_SHARED_RGB_LED)", display)
        self.assertIn("esp_rom_delay_us(80)", led)


if __name__ == "__main__":
    unittest.main()
