# PRG32 Reference Hardware

## Prototype breadboard parts

- ESP32-C6 development board
- ILI9341 2.8 inch SPI TFT, 320x240
- one 5-way digital joystick module for player 1
- optional second 5-way digital joystick module for player 2 games such as Pong
- one setup button, or use the joystick push switch when the wiring allows it
- Passive buzzer
- Jumper wires and breadboard

Desktop QEMU can emulate the PRG32 graphics viewport for early software tests,
but it does not replace this hardware validation. Use the physical board for
LCD wiring, GPIO buttons, buzzer output, and controller bridge wiring.

The resident firmware also starts the `PRG32` Wi-Fi AP for cartridge uploads.
Keep the antenna area of the ESP32-C6 module clear in the enclosure.

## USB controller bridge

ESP32-C6 remains the main RISC-V teaching microcontroller. It does not directly
host arbitrary wired USB HID controllers, so PRG32 uses a bridge:

```text
USB controller -> USB host bridge -> UART -> ESP32-C6
```

The bridge sends the packet documented in `docs/external_controllers.md`:

```text
'U' 'G' <lo> <hi>
```

Suitable bridge boards include ESP32-S3, RP2040 with USB host support,
CH559/CH554, or a PC-side serial helper during labs.

## Reference wiring

| ESP32-C6 signal | Display / device |
|---|---|
| 3V3 | TFT VCC |
| GND | TFT GND |
| GPIO6 | TFT MOSI |
| GPIO7 | TFT MISO / touch DO |
| GPIO5 | TFT SCLK |
| GPIO10 | TFT CS |
| GPIO11 | TFT DC |
| GPIO12 | TFT RST |
| GPIO13 | TFT BL |
| GPIO0 | P1 joystick LEFT to GND |
| GPIO1 | P1 joystick RIGHT to GND |
| GPIO2 | P1 joystick UP to GND |
| GPIO3 | P1 joystick DOWN to GND |
| GPIO4 | P1 joystick SELECT/A to GND |
| GPIO8 | P1 optional B/back button to GND |
| GPIO14 | Setup button to GND |
| GPIO9 | Passive buzzer |

The previous six-tactile-button layout is replaced by a digital joystick module
such as the user-provided reference part:
<https://www.amazon.it/dp/B07HBPW3DF?ref=ppx_yo2ov_dt_b_fed_asin_title>.
Wire each joystick direction as a normally-open switch to ground; PRG32 enables
internal pull-ups.

For two-player games, mount a second joystick and assign its pins through
`PRG32_PIN_P2_LEFT`, `PRG32_PIN_P2_RIGHT`, `PRG32_PIN_P2_UP`,
`PRG32_PIN_P2_DOWN`, `PRG32_PIN_P2_A`, `PRG32_PIN_P2_B`, and
`PRG32_PIN_P2_START` in `main/prg32_config.h`.

The `kicad` directory contains starter placeholders for a KiCad production
board. The `case` directory contains a simple OpenSCAD enclosure starter. The
`prg32_v2` directory contains the next hardware revision scaffold.
