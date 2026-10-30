# PRG32 Hardware Pin Profile: Breadboard 2

This document specifies the pinout and wiring for the **`breadboard2`** hardware profile (`profiles/sdkconfig.pins.breadboard2`), featuring a 2-axis analog thumbstick read via ESP32-C6 continuous ADC mode, an updated display harness, four action buttons (A, B, C, D), and two bumper buttons (L, R).

## Overview

- **Architecture Target**: ESP32-C6
- **Display**: ILI9341 2.8" SPI TFT (320x240) with CS on GPIO9, DC on GPIO10, and RST wired to the ESP32-C6 EN (`RST`) header pin
- **Input Type**: 2-Axis Analog Thumbstick (potentiometers read via ADC1 Continuous mode)
- **Action Buttons**: Separate pushbuttons for A, B, C, D, and START/SELECT (C on GPIO11, D on GPIO3)
- **Bumper Buttons**: L (left bumper) on GPIO21 and R (right bumper) on GPIO22
- **Audio**: MAX98357A I2S DAC / amplifier (Mono or Stereo) with LRC/WS on GPIO15
- **RGB LED**: Off by default (`CONFIG_PRG32_PIN_RGB_LED=-1`); GPIO8 is free, so the DevKitC-1 onboard WS2812 can be enabled
- **Configuration Profile**: `profiles/sdkconfig.pins.breadboard2`

## Pinout and Wiring

### Display (ILI9341 SPI TFT)

| ESP32-C6 GPIO | ILI9341 Signal | Description | Notes |
|---|---|---|---|
| 3V3 | VCC | 3.3V Logic Supply | |
| GND | GND | Ground | |
| GPIO7 | MOSI | SPI Master Out Slave In | Fast SPI (SPI2 IOMUX) |
| GPIO2 | MISO / DO | SPI Master In Slave Out | Fast SPI (SPI2 IOMUX); used to read the display ID at boot |
| GPIO6 | SCLK | SPI Serial Clock | Fast SPI (SPI2 IOMUX) |
| GPIO9 | CS | Display Chip Select | Active-low; GPIO9 is the strapping/BOOT pin, see the warning below |
| GPIO10 | DC | Data / Command Select | |
| EN (`RST` header pin) | RST | Hardware Reset | No GPIO (`CONFIG_PRG32_PIN_LCD_RST=-1`); reset together with the ESP32-C6, see [Display Reset Line](#display-reset-line) |
| GPIO5 | BL / LED | Backlight control | Active-high |

#### Display Reset Line

The ILI9341 RST pin is wired to the ESP32-C6-DevKitC-1 header pin labelled
`RST`, which is the chip enable (EN) input. No GPIO drives the display reset.

- **Hardware reset with the chip.** EN goes low at power-up, when the board's
  RST button is pressed, and when the USB-UART bridge auto-resets the board
  for flashing. While EN is low, the ESP32-C6 is off and the display is held
  in reset; when EN rises, both start together. The ILI9341 needs about 120 ms
  after a reset before it accepts setup commands, and the ESP32-C6 boot alone
  takes longer, so the timing is met. The display reset input tolerates the
  slow EN edge produced by the board's pull-up and capacitor.
- **Software restarts.** `esp_restart()`, panics, and watchdog resets happen
  inside the chip and do not drive EN, so the display gets no hardware reset.
  Every boot runs the display initialization, which raises CS and sends the
  ILI9341 software reset command before configuring the controller, so the
  screen is reset on these restarts too (see the [ILI9341 notes](../ili9341.md#reset-line)).
- **Electrical load.** The display RST input only senses voltage and draws
  almost no current, so it does not hold EN low and the ESP32-C6 boots
  normally. A pull-up resistor on the display module pulls the same way as the
  board's EN pull-up and is harmless.

> [!WARNING]
> Never wire a GPIO to EN, and never connect the display RST to both EN and a
> GPIO. When a GPIO pulls EN low the ESP32-C6 switches itself off and the GPIO
> stops driving, and it fights the RST button and the auto-reset circuit.

### Controls: 2-Axis Analog Joystick, Buttons & Bumpers

The analog joystick connects two potentiometers (X and Y axes) between 3.3V and GND, providing analog voltages to the ESP32-C6 SAR ADC1. Directional detection supports simultaneous multi-axis activation (diagonals).

| ESP32-C6 GPIO | ADC Channel | Input Signal | Description |
|---|---|---|---|
| GPIO0 | ADC1_CH0 | P1 X AXIS | Horizontal axis (0 to 3.3V). Left < 1300 (`LEFT_THRESHOLD`), Right > 2800 (`RIGHT_THRESHOLD`) |
| GPIO1 | ADC1_CH1 | P1 Y AXIS | Vertical axis (0 to 3.3V). Up < 1300 (`UP_THRESHOLD`), Down > 2800 (`DOWN_THRESHOLD`) |
| GPIO20 | - | P1 START / SELECT | Joystick thumbstick switch or button to GND |
| GPIO18 | - | P1 A | Action button A, momentary switch to GND |
| GPIO19 | - | P1 B | Action button B, momentary switch to GND |
| GPIO11 | - | P1 C | Action button C, momentary switch to GND |
| GPIO3 | - | P1 D | Action button D, momentary switch to GND |
| GPIO21 | - | P1 L | Left bumper, momentary switch to GND |
| GPIO22 | - | P1 R | Right bumper, momentary switch to GND |

> [!WARNING]
> GPIO9 is an ESP32-C6 strapping pin: if it is low at reset, the chip starts
> the ROM download mode instead of PRG32. It is also wired to the DevKitC-1
> **BOOT** button. This profile therefore puts the display CS on GPIO9, a
> signal nobody presses: the display input never pulls the pin low, and the
> boot pull-up keeps the display deselected until the driver starts. Pressing
> BOOT while PRG32 runs only holds CS low and may glitch the screen briefly.
> Never wire buttons to GPIO9 or to GPIO12/GPIO13 (the native USB D-/D+ pair
> used for uploads and the device protocol).

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

### Onboard RGB LED

| ESP32-C6 GPIO | Setting | Description |
|---|---|---|
| GPIO8 | (hardwired on the DevKitC-1) | Onboard WS2812 data input; no other signal uses GPIO8 in this profile |
| Not wired (`-1`) | `CONFIG_PRG32_PIN_RGB_LED` | Set to `8` to enable the LED API and the audio visualizer |
| Not set (`-1`) | `CONFIG_PRG32_PIN_SHARED_RGB_LED` | Not needed: the LED does not share a pin with the display (see [Shared Boot Indication](../rgb_led.md#shared-boot-indication)) |

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
python3 -m prg32 esp32c6 build --pin-profile breadboard2

# Flash directly
python3 -m prg32 esp32c6 build --pin-profile breadboard2 && python3 -m prg32 esp32c6 flash

# Direct ESP-IDF build
idf.py -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.pins.breadboard2" build
```

You can also select `breadboard2` interactively in `idf.py menuconfig`:
`Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration` -> `Hardware Pin Profile` -> `breadboard2 (Second Breadboard with Analog Joystick)`.
