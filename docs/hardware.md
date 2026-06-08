# PRG32 Hardware

This page summarizes the classroom reference wiring. See `hardware/README.md`
for the hardware directory map and board scaffolds.

## Base Hardware

| Quantity | Item | Purpose |
|---:|---|---|
| 1 | ESP32-C6 development board | PRG32 RISC-V host |
| 1 | ILI9341 SPI TFT or supported display | video output |
| 1 | digital joystick module | local input |
| 1 | optional setup button | force setup mode without holding A+B |

## Reference Display And Input Wiring

These pins match `main/prg32_config.h` for the ESP32-C6 physical ILI9341 build.
They are the same display wiring used by the Arduino/Adafruit validation sketch.

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
| GPIO18 | P1 LEFT |
| GPIO19 | P1 RIGHT |
| GPIO3 | P1 UP |
| GPIO13 | P1 DOWN |
| GPIO20 | P1 SELECT |
| GPIO21 | P1 A |
| GPIO22 | P1 B |
| GPIO15 | passive buzzer |

GPIO14 is still accepted by the firmware as an older START/SELECT wiring
alias, but new classroom harnesses should use GPIO20 for SELECT.

The LCD backlight defaults to active-high. If a specific breakout uses an
active-low backlight transistor, set `PRG32_LCD_BACKLIGHT_ACTIVE_LEVEL` to `0`
in `main/prg32_config.h`.

## Onboard RGB LED

PRG32 can drive a WS2812-style onboard RGB LED through:

```c
prg32_rgb_led_init(gpio);
prg32_rgb_led_set(red, green, blue);
```

Many ESP32-C6 development boards wire the onboard RGB LED to GPIO8. The
reference PRG32 display harness already uses GPIO8 for LCD D/C, so
`PRG32_PIN_RGB_LED` defaults to `-1` and the LED is disabled. Enable it only
when your board exposes the LED on a free GPIO or your display wiring has been
changed accordingly. The setup audio menu can use the LED as a spectrum-style
VU meter.

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

The QEMU defaults are for the ESP32-C3 virtual display path and intentionally
disable physical I2S audio output.

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

## Safety Notes

- Do not connect speaker outputs to headphones or line inputs.
- Keep speaker wires short during breadboard tests.
- Use a power source that can supply the speaker current.
- Reassign audio GPIOs if they conflict with the display or joystick wiring in
  a specific classroom kit.
