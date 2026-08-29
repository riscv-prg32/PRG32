# PRG32 Hardware

This page summarizes the classroom reference wiring. See
[`docs/pcb/README.md`](pcb/README.md) for the reference PCB design.
This page summarizes the classroom reference wiring. See [`/hardware/README.md`](../../hardware/README.md) for the hardware directory map and board scaffolds.

## PCB Component Layout

The Fritzing reference board is a 210 x 75 mm landscape PCB. Components are
arranged to keep the game controls accessible on the front and the compute and audio hardware on the rear:

- Front, from left to right: digital joystick, landscape ILI9341 display, A
  button, and B button.
- Rear, from left to right when viewed from the rear: left speaker, left
  MAX98357A, ESP32-C6 development board, right MAX98357A, and right speaker.

The right-channel mode resistor is placed beside the right MAX98357A on the
rear. Through-hole pads remain visible from both PCB sides even when the module
body is mounted on the indicated side.

## Reference Prototype Figures

The following figures are exported from the schematic and breadboard views of
`docs/pcb/PRG32-PCB-0-1.fzz`. They describe the same electrical nets and use
the canonical GPIO assignments listed below.

![PRG32 electrical schematic showing the ESP32-C6, ILI9341 display, joystick, A and B buttons, stereo MAX98357A amplifiers, and speakers](../images/prg32-electrical-schematic.svg

*Electrical schematic for the current firmware configuration.*

![PRG32 breadboard prototype with the ESP32-C6 centered across the breadboard, display above it, joystick and buttons at the front, and stereo audio components at the sides](../images/prg32-breadboard-prototype.svg

*Breadboard prototype using the same connections as the electrical schematic.*

In the breadboard view, the ESP32-C6 is horizontal with its long axis parallel
to the center trench. Its two header rows occupy opposite terminal-strip halves,
so the trench keeps the two rows electrically separate. The joystick is placed
on the left, A and B are placed on the right, and the display remains above the
breadboard so its header and screen stay visible. The two audio channels are
routed toward their corresponding left and right speakers.

The drawing follows standard solderless-breadboard continuity: the red and blue
power rails run horizontally, while each numbered five-hole terminal group is
connected vertically on only one side of the center trench. No connection is
assumed across that trench. Some physical breadboards split a power rail near
its midpoint; bridge that split with a jumper when the selected board does not
provide a continuous rail. See the [breadboard design overview on
Wikipedia](https://en.wikipedia.org/wiki/Breadboard#Bus_and_terminal_strips) for
an illustration of the internal strips. Wire colors are visual aids only;
connector labels and the tables below define the electrical connections.

### Breadboard Power Distribution

Use the buses exactly as shown in the breadboard figure. Do not interchange the
3V3 and 5V rails.

| Breadboard bus | Voltage | Connections |
|---|---:|---|
| Upper blue bus | GND | ESP32-C6 GND, ILI9341 GND, joystick/button grounds |
| Upper red bus | 3V3 | ESP32-C6 3V3 and ILI9341 VCC |
| Lower blue bus | GND | Both MAX98357A GND pins |
| Lower red bus | 5V | ESP32-C6 5V and both MAX98357A VIN pins |

Install the black jumper at column 61 between the upper and lower blue buses so
the display, controls, ESP32-C6, and amplifiers share one ground reference. If
the breadboard has a midpoint break in either bus, add a jumper across each
used split as well. The upper red 3V3 bus and lower red 5V bus must remain
separate.

For a reproducible classroom build:

1. Disconnect USB and all other power sources.
2. Place the ESP32-C6 horizontally across the center trench. Confirm that each
   header row is in a different five-hole terminal-strip half.
3. Wire the two ground buses and the column-61 ground bridge first.
4. Wire ESP32-C6 3V3 and the display only to the upper red bus.
5. Wire ESP32-C6 5V and both MAX98357A VIN pins only to the lower red bus.
6. Add each signal jumper to an unused hole in the same five-hole group as its
   corresponding ESP32-C6 pin. Never use the matching numbered group on the
   opposite side of the trench as though it were connected.
7. Before applying power, use continuity mode to verify both ground buses are
   common and verify there is no continuity between the 3V3 and 5V buses.

## Base Hardware

| Quantity | Item | Purpose |
|---:|---|---|
| 1 | ESP32-C6 development board | PRG32 RISC-V host |
| 1 | ILI9341 SPI TFT or supported display | video output |
| 1 | digital joystick module | local input |

## Reference Display And Input Wiring

These pins match `main/prg32_config.h` for the ESP32-C6 physical ILI9341 build.

| ESP32-C6 | ILI9341 TFT / control |
|---|---|
| 3V3 | VCC |
| GND | GND |
| GPIO7 | MOSI |
| GPIO2 | MISO / touch DO |
| GPIO6 | SCLK |
| GPIO10 | CS |
| GPIO1 | DC |
| GPIO0 | RST |
| GPIO5 | BL |

| ESP32-C6 | Input |
|---|---|
| GPIO18 | P1 LEFT, switch to GND |
| GPIO19 | P1 RIGHT, switch to GND |
| GPIO3  | P1 UP, switch to GND |
| GPIO13 | P1 DOWN, switch to GND |
| GPIO20 | P1 START / SELECT, switch to GND |
| GPIO21 | P1 A, switch to GND |
| GPIO22 | P1 B, switch to GND |

The firmware enables the internal pull-up on every button input, so a pressed
button connects its GPIO to GND. START and SELECT are names for the same
GPIO20 input in the default configuration. The reference hardware has no
dedicated SETUP button: hold A+B during startup to enter setup mode. Setup mode
also opens automatically when the firmware cannot select a cartridge to boot.

`PRG32_PIN_BUZZER` is `-1` in the current physical configuration, so no passive
buzzer is wired or initialized. Audio uses the I2S amplifier described below.

The LCD backlight defaults to active-high. If a specific breakout uses an
active-low backlight transistor, set `PRG32_LCD_BACKLIGHT_ACTIVE_LEVEL` to `0`
in `main/prg32_config.h`.

## Onboard RGB LED

PRG32 can drive a WS2812-style onboard RGB LED through:

```c
prg32_rgb_led_init(gpio);
prg32_rgb_led_set(red, green, blue);
```

The physical firmware sets `PRG32_PIN_RGB_LED` to GPIO8 for the WS2812-style
onboard LED. GPIO8 does not overlap the current display harness, whose LCD D/C
line is GPIO1. The setup audio menu can use the LED as a spectrum-style VU
meter. QEMU builds set the RGB LED pin to `-1` and do not initialize physical
LED hardware.

## Mono Audio

| ESP32-C6 | MAX98357A |
|---|---|
| 3V3 or 5V | VIN |
| GND | GND |
| GPIO4 | BCLK |
| GPIO11 | LRC / WS |
| GPIO23 | DIN |
| not wired by default | SD / MODE optional |

Connect one 4-8 ohm speaker to the MAX98357A speaker `+` and `-` outputs.

The default audio Kconfig pins avoid the reference display, joystick, and
passive buzzer wiring. If a breakout needs explicit SD/shutdown control, assign
`CONFIG_PRG32_AUDIO_I2S_SD_GPIO` to another unused GPIO before flashing.

On the Adafruit MAX98357A breakout, `SD` also selects the channel mode. Leave it
in the breakout's default enabled state for mono, or drive it from the optional
SD GPIO. Do not tie `SD` directly to GND because that shuts the amplifier down.
PRG32 mono audio is carried as duplicated left/right I2S slots, so a single
MAX98357A works whether the breakout averages both slots or selects one slot.

Use the physical ESP32-C6 defaults when flashing classroom hardware:

```bash
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build flash monitor
```

The QEMU defaults are for the ESP32-C3 virtual display path. They set all
physical display, button, setup, buzzer, and RGB LED pins to `-1`; keyboard
input arrives through the QEMU UART console. Audio remains enabled in the QEMU
configuration but does not use the ESP32-C6 classroom wiring documented here.

## Stereo Audio

Stereo uses two MAX98357A boards on the same I2S bus:

| ESP32-C6 | Left MAX98357A | Right MAX98357A |
|---|---|---|
| 3V3 or 5V | VIN | VIN |
| GND | GND | GND |
| GPIO4 | BCLK | BCLK |
| GPIO11 | LRC / WS | LRC / WS |
| GPIO23 | DIN | DIN |
| optional SD GPIO | SD | SD |

Configure the left board for left output and the right board for right output.
Breakout pin labels vary; verify the vendor pinout before soldering.

By changing the voltage on the SD pin, you are telling the amplifier which data stream (that the LRC pin just identified) to output to your speaker:
- Left + Right Average: Voltage between 0.16V and 0.77V
- Right Channel Only: Voltage between 0.77V and 1.4V
- Left Channel Only: Voltage higher than 1.4V
- Shutdown (Mute): Grounded (Under 0.16V)

Instead of using an additional GPIO for the SD, you can use the VIN with no resistance for the left channel speaker and with an appropriate resistance to target the right channel speaker.


## Safety Notes

- Do not connect speaker outputs to headphones or line inputs.
- Keep speaker wires short during breadboard tests.
- Use a power source that can supply the speaker current.
- Reassign audio GPIOs if they conflict with the display or joystick wiring in
  a specific classroom kit.
