# PRG32 Hardware Pin Profiles

PRG32 completely decouples GPIO pin assignments from hardcoded Kconfig defaults. All physical hardware pin assignments are defined exclusively in profile configuration files located in `profiles/sdkconfig.pins.*`.

This directory contains detailed wiring diagrams, schematics, and pinout tables for each supported physical hardware configuration profile.

## Available Profiles

| Profile Name | Description | Display Pinout | Controller / Input | Audio |
|---|---|---|---|---|
| [`prototype1`](prototype1.md) | First breadboard prototype | ILI9341 SPI (DC: GPIO8, RST: GPIO9) | 5-way digital navigation joystick (discrete GPIO switches on GPIO3, 13, 18, 19, 20) + buttons A (21), B (22) | MAX98357A I2S (BCLK: 4, LRCLK: 11, DIN: 23) |
| [`prototype2`](prototype2.md) | Second breadboard prototype with analog joystick | ILI9341 SPI (DC: GPIO15, RST: GPIO12, SD CS: GPIO9) | 2-axis analog thumbstick (ADC1 continuous mode on GPIO0, GPIO1) + buttons A (18), B (19), START/SELECT (20) | MAX98357A I2S (BCLK: 4, LRCLK: 11, DIN: 23) |

## Selecting a Profile

### Via PRG32 CLI
```bash
# List all available profiles
python3 -m prg32 esp32c6 pins

# Build with a specific profile
python3 -m prg32 esp32c6 build --pin-profile prototype1
python3 -m prg32 esp32c6 build --pin-profile prototype2
```

### Via Menuconfig
1. Run `idf.py menuconfig`.
2. Navigate to `Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration` -> `Hardware Pin Profile`.
3. Choose either `prototype1` or `prototype2`.

> [!NOTE]
> Hardware profiles only define the pins for their active joystick subsystem:
> - In digital joystick profiles (`CONFIG_PRG32_JOYSTICK_DIGITAL=y`), analog ADC pins (`CONFIG_PRG32_PIN_JOYSTICK_X/Y`) are not required and omitted.
> - In analog joystick profiles (`CONFIG_PRG32_JOYSTICK_ANALOG=y`), discrete digital directional pins (`CONFIG_PRG32_PIN_BTN_UP/DOWN/LEFT/RIGHT`) are not required and omitted.
> The firmware automatically inspects the active joystick type flag and only reads the relevant pins.
