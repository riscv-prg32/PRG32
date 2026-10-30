# PRG32 Hardware Pin Profiles

PRG32 completely decouples GPIO pin assignments from hardcoded Kconfig defaults. All physical hardware pin assignments are defined exclusively in profile configuration files located in `profiles/sdkconfig.pins.*`.

This directory contains detailed wiring diagrams, schematics, and pinout tables for each supported physical hardware configuration profile.

## Available Profiles

| Profile Name | Description | Display Pinout | Controller / Input | Audio |
|---|---|---|---|---|
| [`breadboard1`](breadboard1.md) | First breadboard | ILI9341 SPI (DC: GPIO8, RST: GPIO9) | 5-way digital navigation joystick (discrete GPIO switches on GPIO3, 0, 18, 19, 20) + buttons A (21), B (22) | MAX98357A I2S (BCLK: 4, LRCLK: 11, DIN: 23) |
| [`breadboard2`](breadboard2.md) | Second breadboard with analog joystick | ILI9341 SPI (CS: GPIO9, DC: GPIO10, RST: ESP32-C6 EN pin) | 2-axis analog thumbstick (ADC1 continuous mode on GPIO0, GPIO1) + buttons A (18), B (19), C (11), D (3), START/SELECT (20) | MAX98357A I2S (BCLK: 4, LRCLK: 15, DIN: 23) |
| [`pcb1`](pcb1.md) | Soldered PCB | ILI9341 SPI (DC: GPIO8, RST: GPIO9) | 5-way digital navigation joystick (discrete GPIO switches on GPIO3, 13, 18, 19, 20) + buttons A (21), B (22) | MAX98357A I2S (BCLK: 4, LRCLK: 11, DIN: 23) |

Profiles named `breadboardN` describe solderless breadboard builds; their
parts are listed in the [bill of materials](../where_to_buy.md). Profiles named
`pcbN` describe boards that are already soldered and have no purchasing list.

## Selecting a Profile

### Via PRG32 CLI
```bash
# List all available profiles
python3 -m prg32 esp32c6 pins

# Build with a specific profile
python3 -m prg32 esp32c6 build --pin-profile breadboard1
python3 -m prg32 esp32c6 build --pin-profile breadboard2
python3 -m prg32 esp32c6 build --pin-profile pcb1
```

### Via Menuconfig
1. Run `idf.py menuconfig`.
2. Navigate to `Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration` -> `Hardware Pin Profile`.
3. Choose `breadboard1`, `breadboard2`, or `pcb1`.

> [!NOTE]
> Hardware profiles only define the pins for their active joystick subsystem:
> - In digital joystick profiles (`CONFIG_PRG32_JOYSTICK_DIGITAL=y`), analog ADC pins (`CONFIG_PRG32_PIN_JOYSTICK_X/Y`) are not required and omitted.
> - In analog joystick profiles (`CONFIG_PRG32_JOYSTICK_ANALOG=y`), discrete digital directional pins (`CONFIG_PRG32_PIN_BTN_UP/DOWN/LEFT/RIGHT`) are not required and omitted.
> The firmware automatically inspects the active joystick type flag and only reads the relevant pins.

## Writing a Pin Profile Document

Every `profiles/sdkconfig.pins.<name>` file has exactly one document here,
`<name>.md`. A reader must be able to build, wire, or debug that hardware
from this one document alone.

### Rules

1. **Self-contained.** Write out every pin, wiring table, and power connection
   in full. Never describe a profile as differences from another profile and
   never send the reader to another profile for wiring. Link component guides
   only for behavior (driver internals, API, thresholds), not for pins.
2. **Matches the pin file exactly.** Every GPIO in the document equals the
   value in `profiles/sdkconfig.pins.<name>`. Options the file leaves out are
   documented with their Kconfig default (for example
   `CONFIG_PRG32_PIN_BUZZER=-1`). Unused signals are shown as
   `Not wired (-1)`, never omitted.
3. **One place for pins.** GPIO numbers and pinout tables belong only in these
   documents and the pin files. Component guides (`audio.md`, `joystick.md`,
   `ili9341.md`, ...) describe behavior and link here.
4. **Figures show this profile.** Embed a schematic or breadboard figure whose
   wires match this profile's pins, stored in
   [`docs/hardware/images/`](../images/). Never reuse another profile's figure
   when any drawn pin differs. Caption where the figure comes from (Fritzing
   export, edited SVG) and its revision.
5. **Breadboard vs PCB.** `breadboardN` documents include breadboard power
   rails and assembly steps; their parts go in the
   [bill of materials](../where_to_buy.md). `pcbN` documents include the board
   layout, power nets, and pre-power checks instead, and never add parts to
   the bill of materials.
6. **Warn about pin hazards** in a `[!WARNING]` block near the top: native USB
   D-/D+ (GPIO12/GPIO13), pins shared with onboard parts (the DevKitC-1 WS2812
   on GPIO8), 3.3V-only inputs such as ADC pins.

### Required Sections

Use this order and these headings:

| Section | Content |
|---|---|
| `# PRG32 Hardware Pin Profile: <Name>` | Intro naming the profile and its pin file, then the figure and any `[!WARNING]` |
| `## Overview` | Target, board, display, input, buttons, audio, RGB LED, and configuration profile, one line each |
| `## Board Layout` (PCB) | Component placement on each side |
| `## Pinout and Wiring` | One `###` table per subsystem: display, controls, audio (mono and stereo), RGB LED; columns start with `ESP32-C6 GPIO` |
| `### GPIO Usage Summary` | Every exposed GPIO with its function or `Free` |
| `## Breadboard Power Distribution & Assembly` (breadboard) or `## Power Distribution` (PCB) | Rails or nets, voltages, connections; assembly steps or pre-power checks |
| `## Profile File` | The settings of `profiles/sdkconfig.pins.<name>` and the defaults it relies on |
| `## Software & Build Selection` | `python3 -m prg32 esp32c6 build --pin-profile <name>`, the direct `idf.py` command, and the exact menuconfig entry |
| `## History` (optional) | Commits that shaped the profile and when to choose it |
| `## Related Documentation` | Component guides for behavior |

### Adding a Profile

1. Create `profiles/sdkconfig.pins.<name>` and its
   `CONFIG_PRG32_PIN_PROFILE_<NAME>` choice in
   [`components/prg32/Kconfig`](../../../components/prg32/Kconfig); map the
   symbol to the name in `CMakeLists.txt` and
   `prg32/esp32c6/build_esp32c6.py`.
2. Write `<name>.md` following the rules above and add a row to
   [Available Profiles](#available-profiles).
3. Update the profile lists in the [hardware overview](../README.md),
   [usage profiles](../../usage/profiles.md),
   [configuration](../../usage/configuration.md), and the
   [repository structure](../../repository_structure.md).
4. For a `breadboardN` profile, update the [bill of materials](../where_to_buy.md).
