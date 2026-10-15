# PRG32 Hardware

This page is the canonical wiring guide for the physical ESP32-C6 firmware
profile. It describes the default values in `components/prg32/Kconfig`,
`components/prg32_audio/Kconfig`, and `profiles/sdkconfig.defaults.esp32c6`.
For the matching PCB reference design, see
[`docs/hardware/pcb/README.md`](/docs/hardware/pcb/README.md).

![PRG32 breadboard prototype with the ESP32-C6 centered across the breadboard, display above it, joystick and buttons at the front, and stereo audio components at the sides](images/prg32-breadboard-prototype.svg)

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

## Safety Notes

- Do not connect speaker outputs to headphones or line inputs.
- Keep speaker wires short during breadboard tests.
- Use a power source that can supply the speaker current.
- Reassign audio GPIOs if they conflict with the display or joystick wiring in
  a specific classroom kit.

## Pinouts, Wiring, and Profiles

All physical GPIO pin assignments, wiring tables, breadboard power distribution, and assembly instructions are maintained under [`profiles/`](profiles/) as the single source of truth for hardware setups:

- [**Prototype 1 Profile (`prototype1`)**](profiles/prototype1.md): Reference breadboard prototype featuring discrete 5-way digital navigation joystick switches, reference ILI9341 wiring (DC: GPIO8, RST: GPIO9), power bus distribution, and I2S audio.
- [**Prototype 2 Profile (`prototype2`)**](profiles/prototype2.md): Second breadboard prototype featuring a 2-axis analog thumbstick (continuous ADC mode on GPIO0 and GPIO1), updated display harness (DC: GPIO15, RST: GPIO12), reserved SD card CS (GPIO9, see [MicroSD Card Guide](sdcard.md)), power bus distribution, and I2S audio.
- [**Hardware Profiles Directory**](profiles/README.md): Complete comparison table, architecture mapping, and guide for switching hardware profiles.

> [!IMPORTANT]
> **Single Source of Truth**: Hardware pin setups, wiring schematics, and board profiles are exclusively defined and maintained in [`docs/hardware/profiles/`](profiles/) and [`profiles/sdkconfig.pins.*`](../../profiles/). Do not duplicate pinout tables or wiring schemes elsewhere in documentation.

### Base Hardware

See the [bill of materials and country-specific purchasing links](where_to_buy.md)
for the complete mono build, optional stereo parts, and assembly supplies.

## Build Steps & Assembly

Detailed step-by-step breadboard assembly and power distribution guidelines for each build are documented in their respective profile guides:
- [Prototype 1 Breadboard Assembly & Power Distribution](profiles/prototype1.md#breadboard-power-distribution--assembly)
- [Prototype 2 Breadboard Assembly & Power Distribution](profiles/prototype2.md#breadboard-power-distribution--assembly)

### Stereo Configuration (Supplementary Info)

Stereo uses two MAX98357A boards on the same I2S bus. Configure the left board for left output and the right board for right output.
Breakout pin labels vary; verify the vendor pinout before soldering.

By changing the voltage on the SD pin, you are telling the amplifier which data stream (that the LRC pin just identified) to output to your speaker:
- Left + Right Average: Voltage between 0.16V and 0.77V
- Right Channel Only: Voltage between 0.77V and 1.4V
- Left Channel Only: Voltage higher than 1.4V
- Shutdown (Mute): Grounded (Under 0.16V)

In the reference stereo wiring, connect the right MAX98357A `SD/MODE` pin to
`VIN` through a **390 kΩ resistor** to select the right channel. The left
module uses the breakout's left-channel selection. The 390 kΩ value applies
to the reference module and 5 V supply shown in the schematic; verify the
`SD/MODE` voltage for another breakout or supply before substituting parts.
See the [bill of materials](where_to_buy.md) for the resistor and footprint.

## Software Configuration

Physical hardware pin assignments are decoupled from Kconfig defaults and defined in profile files under `profiles/` (see [PRG32 Profiles](../usage/profiles.md#hardware-pin-profiles)).

Hardware pin configurations default to "None" in Kconfig so that builds without a specified profile halt immediately at build time.

You can select a pin setup either interactively or via the CLI:
- **Interactive Menuconfig**: Open `idf.py menuconfig`, navigate to `Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration` -> `Hardware Pin Profile`, and choose:
  - `prototype1 (First Prototype Breadboard)` for [Prototype 1](profiles/prototype1.md)
  - `prototype2 (Second Prototype Breadboard with Analog Joystick)` for [Prototype 2](profiles/prototype2.md)
  - Or select `Custom Profile` to configure GPIO numbers manually in `menuconfig`.
- **PRG32 CLI**:
  ```bash
  # See available pin setup possibilities
  python3 -m prg32 esp32c6 pins

  # Build specifying the profile
  python3 -m prg32 esp32c6 build --pin-profile prototype1
  python3 -m prg32 esp32c6 build --pin-profile prototype2
  ```
- **Direct ESP-IDF Build**: Append the profile fragment to `SDKCONFIG_DEFAULTS`:
  ```bash
  idf.py -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.pins.prototype1" build
  ```


The firmware enables the internal pull-up on every button input, so a pressed
button connects its GPIO to GND. START and SELECT are names for the same
GPIO20 input in the default configuration. The reference hardware has no
dedicated SETUP button. The shipped profile sets `PRG32_BOOT_SETUP_MODE=1`, so
setup opens automatically on every boot. If that option is changed to `0`,
setup still opens when the firmware cannot select a cartridge. Holding A+B can
force setup only when `CONFIG_PRG32_ENABLE_BOOT_SETUP_COMBO=y`; that shortcut
is disabled by default.

`PRG32_PIN_BUZZER` is `-1` in the current physical configuration, so no passive
buzzer is wired or initialized. Audio uses the I2S amplifier described below.

The LCD backlight defaults to active-high. If a specific breakout uses an
active-low backlight transistor, set `PRG32_LCD_BACKLIGHT_ACTIVE_LEVEL` to `0`
in `idf.py menuconfig`.

### Onboard RGB LED

PRG32 can drive a WS2812-style onboard RGB LED through:

```c
prg32_rgb_led_init(gpio);
prg32_rgb_led_set(red, green, blue);
```

The fixed `prototype1` PCB connects both LCD D/C and the DevKitC onboard RGB
LED data input to GPIO8. The firmware therefore keeps the normal cartridge LED
API disabled (`PRG32_PIN_RGB_LED=-1`), flashes the shared LED cyan three times
during early boot, and sends an off frame after LCD updates. Because the two
devices are electrically tied together, active LCD D/C traffic can still be
interpreted by the WS2812 as brief green or white flashes. Software cannot make
the LED independently controllable on this PCB; do not change the canonical
LCD pin merely to hide those flashes. A future PCB revision should give LCD D/C
and RGB separate GPIOs. QEMU does not initialize physical LED hardware.

### Audio Configuration

The shipped profiles enable I2S audio at 22,050 Hz with eight voices and mono
output. The default audio Kconfig pins avoid the reference display, joystick,
and passive buzzer wiring. If a breakout needs explicit SD/shutdown control,
assign `CONFIG_PRG32_AUDIO_I2S_SD_GPIO` to another unused GPIO before flashing.
Select `CONFIG_PRG32_AUDIO_MODE_STEREO=y` when assembling the optional two-
amplifier stereo circuit.

On the Adafruit MAX98357A breakout, `SD` also selects the channel mode. Leave it
in the breakout's default enabled state for mono, or drive it from the optional
SD GPIO. Do not tie `SD` directly to GND because that shuts the amplifier down.
PRG32 mono audio is carried as duplicated left/right I2S slots, so a single
MAX98357A works whether the breakout averages both slots or selects one slot.

The QEMU defaults are for the ESP32-C3 virtual display path. At runtime,
`main/prg32_config.h` sets all physical display, button, setup, buzzer, and RGB
LED pins to `-1`; keyboard input arrives through the QEMU UART console. Audio
remains enabled in the QEMU configuration but uses the emulator audio path,
not the ESP32-C6 classroom wiring documented here.

## Troubleshooting

- Add each signal jumper to an unused hole in the same five-hole group as its
  corresponding ESP32-C6 pin. Never use the matching numbered group on the
  opposite side of the trench as though it were connected.
- Before applying power, use continuity mode to verify both ground buses are
  common and verify there is no continuity between the 3V3 and 5V buses.
