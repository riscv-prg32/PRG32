# PRG32 Hardware Pin Profile: Breadboard 1

This document specifies the pinout and wiring for the **`breadboard1`** hardware profile (`profiles/sdkconfig.pins.breadboard1`), representing the reference first breadboard build.

![PRG32 breadboard prototype with the ESP32-C6 centered across the breadboard, display above it, digital joystick and buttons at the front, and stereo audio components at the sides](../images/prg32-breadboard-prototype.svg)

*Breadboard breadboard 1 using discrete 5-way digital joystick switches and ILI9341 display.*

## Overview

- **Architecture Target**: ESP32-C6
- **Display**: ILI9341 2.8" SPI TFT (320x240)
- **Input Type**: Digital 5-way navigation joystick (discrete GPIO switches with pull-ups)
- **Audio**: MAX98357A I2S DAC / amplifier (Mono or Stereo)
- **RGB LED**: DevKitC-1 onboard WS2812 shared with LCD D/C on GPIO8, boot indication only (`CONFIG_PRG32_PIN_SHARED_RGB_LED=8`, `CONFIG_PRG32_PIN_RGB_LED=-1`)
- **Configuration Profile**: `profiles/sdkconfig.pins.breadboard1`

## Pinout and Wiring

### Display (ILI9341 SPI TFT)

| ESP32-C6 GPIO | ILI9341 Signal | Description |
|---|---|---|
| 3V3 | VCC | 3.3V Logic Supply |
| GND | GND | Ground |
| GPIO7 | MOSI | SPI Master Out Slave In |
| GPIO2 | MISO / DO | SPI Master In Slave Out (Touch / display DO) |
| GPIO6 | SCLK | SPI Serial Clock |
| GPIO10 | CS | Display Chip Select (active-low) |
| GPIO8 | DC | Data / Command Select |
| GPIO9 | RST | Hardware Reset (active-low) |
| GPIO5 | BL / LED | Backlight control (active-high) |

### Controls: Digital 5-Way Joystick & Buttons

The firmware enables internal pull-ups on every input. Direction and button presses short the pin to GND.

| ESP32-C6 GPIO | Input Signal | Description |
|---|---|---|
| GPIO18 | P1 LEFT | Left direction, switch to GND |
| GPIO19 | P1 RIGHT | Right direction, switch to GND |
| GPIO3 | P1 UP | Up direction, switch to GND |
| GPIO0 | P1 DOWN | Down direction, switch to GND (GPIO12/GPIO13 stay free for native USB D-/D+) |
| GPIO20 | P1 START / SELECT | Start / Select press, switch to GND |
| GPIO21 | P1 A | A button, switch to GND |
| GPIO22 | P1 B | B button, switch to GND |

### Onboard RGB LED

| ESP32-C6 GPIO | Signal | Description |
|---|---|---|
| GPIO8 | Shared RGB LED (`CONFIG_PRG32_PIN_SHARED_RGB_LED=8`) | The DevKitC-1 WS2812 data input shares GPIO8 with LCD D/C; the firmware uses it only for the boot flash and the off frame after each display update (see [Shared Boot Indication](../rgb_led.md#shared-boot-indication)) |
| Not wired (`-1`) | `CONFIG_PRG32_PIN_RGB_LED` | No dedicated RGB LED visualizer pin |

### Audio (I2S MAX98357A)

#### Mono Configuration
| ESP32-C6 GPIO | MAX98357A Signal | Description |
|---|---|---|
| 3V3 or 5V | VIN | Power supply |
| GND | GND | Ground |
| GPIO4 | BCLK | I2S Bit Clock |
| GPIO11 | LRC / WS | I2S Word Select / Left-Right Clock |
| GPIO23 | DIN | I2S Serial Data |
| Not wired (`-1`) | SD / MODE | Optional shutdown / mode |

#### Stereo Configuration
| ESP32-C6 GPIO | Left MAX98357A | Right MAX98357A | Description |
|---|---|---|---|
| 3V3 or 5V | VIN | VIN | Power supply |
| GND | GND | GND | Common ground |
| GPIO4 | BCLK | BCLK | Shared I2S Bit Clock |
| GPIO11 | LRC / WS | LRC / WS | Shared I2S Word Select |
| GPIO23 | DIN | DIN | Shared I2S Data |
| Same as VIN (> 1.4V) | SD / MODE | - | Left channel select |
| 0.77V to 1.4V (390 kΩ to VIN) | - | SD / MODE | Right channel select |

## Breadboard Power Distribution & Assembly

Use the power buses on the solderless breadboard as follows. Do not interchange the 3V3 and 5V rails:

| Breadboard bus | Voltage | Connections |
|---|---:|---|
| Upper blue bus | GND | ESP32-C6 GND, ILI9341 GND, joystick/button grounds |
| Upper red bus | 3V3 | ESP32-C6 3V3 and ILI9341 VCC |
| Lower blue bus | GND | Both MAX98357A GND pins |
| Lower red bus | 5V | ESP32-C6 5V and both MAX98357A VIN pins |

### Assembly Steps

1. Disconnect USB and all other power sources.
2. Place the ESP32-C6 horizontally across the center trench. Confirm that each header row is in a different five-hole terminal-strip half.
3. Wire the two ground buses and the column-61 ground bridge first. Install the black jumper at column 61 between the upper and lower blue buses so the display, controls, ESP32-C6, and amplifiers share one ground reference. If the breadboard has a midpoint break in either bus, add a jumper across each used split as well. The upper red 3V3 bus and lower red 5V bus must remain separate.
4. Wire ESP32-C6 3V3 and the display only to the upper red bus.
5. Wire ESP32-C6 5V and both MAX98357A VIN pins only to the lower red bus.
6. Connect one 4-8 ohm speaker to the MAX98357A speaker `+` and `-` outputs.

## Software & Build Selection

To compile physical firmware using this profile:

```bash
# PRG32 CLI
python3 -m prg32 esp32c6 build --pin-profile breadboard1

# Flash directly
python3 -m prg32 esp32c6 build --pin-profile breadboard1 && python3 -m prg32 esp32c6 flash

# Direct ESP-IDF build
idf.py -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.pins.breadboard1" build
```
