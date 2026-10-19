# PRG32 Hardware Pin Profile: Prototype 2

This document specifies the pinout and wiring for the **`prototype2`** hardware profile (`profiles/sdkconfig.pins.prototype2`), featuring a 2-axis analog thumbstick read via ESP32-C6 continuous ADC mode, an updated display harness, and a reserved pin for SD card SPI Chip Select.

## Overview

- **Architecture Target**: ESP32-C6
- **Display**: ILI9341 2.8" SPI TFT (320x240) with CS on GPIO3, DC on GPIO10, and RST on GPIO11
- **SD Card CS**: GPIO9 (reserved for SPI SD Card; software implementation planned)
- **Input Type**: 2-Axis Analog Thumbstick (potentiometers read via ADC1 Continuous mode)
- **Action Buttons**: Separate pushbuttons for A, B, and START/SELECT
- **Audio**: MAX98357A I2S DAC / amplifier (Mono or Stereo) with LRC/WS on GPIO15
- **Configuration Profile**: `profiles/sdkconfig.pins.prototype2`

## Pinout and Wiring

### Display (ILI9341 SPI TFT)

| ESP32-C6 GPIO | ILI9341 Signal | Description | Notes |
|---|---|---|---|
| 3V3 | VCC | 3.3V Logic Supply | |
| GND | GND | Ground | |
| GPIO7 | MOSI | SPI Master Out Slave In | Fast SPI (SPI2 IOMUX), shared with SD Card MOSI |
| GPIO2 | MISO / DO | SPI Master In Slave Out | Fast SPI (SPI2 IOMUX), shared with SD Card MISO |
| GPIO6 | SCLK | SPI Serial Clock | Fast SPI (SPI2 IOMUX), shared with SD Card SCLK |
| GPIO3 | CS | Display Chip Select | Active-low |
| GPIO10 | DC | Data / Command Select | |
| GPIO11 | RST | Hardware Reset | |
| GPIO5 | BL / LED | Backlight control | Active-high |
| GPIO9 | SD CARD CS | SD Card Chip Select | Reserved pin; see [SD Card Notes](../sdcard.md) |

### Controls: 2-Axis Analog Joystick & Buttons

The analog joystick connects two potentiometers (X and Y axes) between 3.3V and GND, providing analog voltages to the ESP32-C6 SAR ADC1. Directional detection supports simultaneous multi-axis activation (diagonals).

| ESP32-C6 GPIO | ADC Channel | Input Signal | Description |
|---|---|---|---|
| GPIO0 | ADC1_CH0 | P1 X AXIS | Horizontal axis (0 to 3.3V). Left < 1300 (`LEFT_THRESHOLD`), Right > 2800 (`RIGHT_THRESHOLD`) |
| GPIO1 | ADC1_CH1 | P1 Y AXIS | Vertical axis (0 to 3.3V). Up < 1300 (`UP_THRESHOLD`), Down > 2800 (`DOWN_THRESHOLD`) |
| GPIO20 | - | P1 START / SELECT | Joystick thumbstick switch or button to GND |
| GPIO18 | - | P1 A | Action button A, momentary switch to GND |
| GPIO19 | - | P1 B | Action button B, momentary switch to GND |

> [!NOTE]
> The firmware reads ADC1 channels continuously in DMA mode (`adc_continuous`). The raw 12-bit samples (0–4095, center ~2048) are converted into standard PRG32 directional button bits (`PRG32_BTN_LEFT`, `PRG32_BTN_RIGHT`, `PRG32_BTN_UP`, `PRG32_BTN_DOWN`) using dedicated, configurable thresholds for both axes:
> - **Horizontal (X)**: `s_last_raw_x < LEFT_THRESHOLD` sets `PRG32_BTN_LEFT`; `s_last_raw_x > RIGHT_THRESHOLD` sets `PRG32_BTN_RIGHT`.
> - **Vertical (Y)**: `s_last_raw_y < UP_THRESHOLD` sets `PRG32_BTN_UP`; `s_last_raw_y > DOWN_THRESHOLD` sets `PRG32_BTN_DOWN`.
> - Diagonals (e.g., UP + RIGHT) are fully supported with simultaneous bit assertions. Axis inversion is also automatically detected if a low threshold exceeds a high threshold.

### Audio (I2S MAX98357A)

The audio configuration uses MAX98357A I2S DAC/Amp with LRCLK/WS on GPIO15.

#### Mono Configuration
| ESP32-C6 GPIO | MAX98357A Signal | Description |
|---|---|---|
| 3V3 or 5V | VIN | Power supply |
| GND | GND | Ground |
| GPIO4 | BCLK | I2S Bit Clock |
| GPIO15 | LRC / WS | I2S Word Select / Left-Right Clock |
| GPIO23 | DIN | I2S Serial Data |
| Not wired (`-1`) | SD / MODE | Optional shutdown / mode |

#### Stereo Configuration
| ESP32-C6 GPIO | Left MAX98357A | Right MAX98357A | Description |
|---|---|---|---|
| 3V3 or 5V | VIN | VIN | Power supply |
| GND | GND | GND | Common ground |
| GPIO4 | BCLK | BCLK | Shared I2S Bit Clock |
| GPIO15 | LRC / WS | LRC / WS | Shared I2S Word Select |
| GPIO23 | DIN | DIN | Shared I2S Data |
| Same as VIN (> 1.4V) | SD / MODE | - | Left channel select |
| 0.77V to 1.4V (390 kΩ to VIN) | - | SD / MODE | Right channel select |

## Breadboard Power Distribution & Assembly

Use the power buses on the solderless breadboard as follows:

| Breadboard bus | Voltage | Connections |
|---|---:|---|
| Upper blue bus | GND | ESP32-C6 GND, ILI9341 GND, analog joystick GND, button grounds |
| Upper red bus | 3V3 | ESP32-C6 3V3, ILI9341 VCC, and analog joystick VCC (potentiometers) |
| Lower blue bus | GND | Both MAX98357A GND pins |
| Lower red bus | 5V | ESP32-C6 5V and both MAX98357A VIN pins |

### Assembly Steps

1. Disconnect USB and all other power sources.
2. Place the ESP32-C6 horizontally across the center trench. Confirm that each header row is in a different five-hole terminal-strip half.
3. Wire the two ground buses and ground bridge first so all components share a single common ground reference.
4. Wire ESP32-C6 3V3, the ILI9341 display VCC, and the analog joystick potentiometer supply pins (X and Y axis VCC) only to the upper red (3.3V) bus. Never connect the analog joystick potentiometers to 5V, as ESP32-C6 ADC inputs tolerate a maximum of 3.3V.
5. Wire ESP32-C6 5V and both MAX98357A VIN pins only to the lower red (5V) bus.
6. Connect the speakers to the MAX98357A audio outputs.

## Software & Build Selection

To compile physical firmware using this profile:

```bash
# PRG32 CLI
python3 -m prg32 esp32c6 build --pin-profile prototype2

# Flash directly
python3 -m prg32 esp32c6 build --pin-profile prototype2 && python3 -m prg32 esp32c6 flash

# Direct ESP-IDF build
idf.py -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.pins.prototype2" build
```

You can also select `prototype2` interactively in `idf.py menuconfig`:
`Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration` -> `Hardware Pin Profile` -> `prototype2 (Second Prototype Breadboard with Analog Joystick)`.
