from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def test_breadboard1_preserves_fixed_lcd_and_rgb_wiring():
    profile = (ROOT / "profiles/sdkconfig.pins.breadboard1").read_text()
    assert "CONFIG_PRG32_PIN_LCD_DC=8" in profile
    assert "CONFIG_PRG32_PIN_RGB_LED=-1" in profile
    assert "CONFIG_PRG32_PIN_SHARED_RGB_LED=8" in profile


def test_pcb1_uses_the_same_shared_rgb_led_as_breadboard1():
    profile = (ROOT / "profiles/sdkconfig.pins.pcb1").read_text()
    assert "CONFIG_PRG32_PIN_LCD_DC=8" in profile
    assert "CONFIG_PRG32_PIN_RGB_LED=-1" in profile
    assert "CONFIG_PRG32_PIN_SHARED_RGB_LED=8" in profile


def test_shared_led_is_boot_only_and_relatched_after_lcd_updates():
    system = (ROOT / "components/prg32/prg32_system.c").read_text()
    display = (ROOT / "components/prg32/prg32_display_ili9341.c").read_text()
    led = (ROOT / "components/prg32/prg32_led.c").read_text()
    assert "prg32_shared_rgb_led_boot_flash(PRG32_PIN_SHARED_RGB_LED)" in system
    assert "prg32_shared_rgb_led_off(PRG32_PIN_SHARED_RGB_LED)" in display
    assert "esp_rom_delay_us(80)" in led
