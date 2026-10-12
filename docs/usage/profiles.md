# PRG32 Profiles

PRG32 uses different SDK configuration profiles to adjust how the resident firmware is built. While some profiles adjust the available RAM for uploadable cartridges, others configure the framework for different hardware targets (like the QEMU emulator) or enable runtime features like metrics streaming. The active profile dictates the cartridge size limits, available drawing features, and the target platform.

## Base Profiles

- **Classroom Profile (32 KiB)**: An optional smaller window for builds that
  prioritize resident-runtime heap.
- **Extended Profile (64 KiB)**: The default for physical ESP32-C6 and QEMU
  builds. The default stored package limit is also 64 KiB.
- **QEMU Emulator Profile**: The default used by QEMU emulator builds (`profiles/sdkconfig.defaults.qemu`). It configures the system for the emulator (using `esp32c3` as the target, enabling the RGB display, UART input, and failure on real hardware) and extends cartridge RAM to 64 KiB. Use with:
  ```bash
  idf.py -B build-qemu -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.qemu" set-target esp32c3
  ```

## Optional Profiles

### 128 KiB ESP32-C6 Profile (`sdkconfig.defaults.esp32c6_128k`)

The optional physical-board profile reserves 128 KiB of executable cartridge
RAM and uses the ILI9341 low-memory renderer: the persistent framebuffer is
the 320x200 game viewport, while the two 20-pixel status bands are generated
during LCD transfer. This keeps the game coordinate system unchanged. Full
320x240 framebuffer drawing is therefore not available in this profile.

```bash
idf.py -B build-esp32c6-128k \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.defaults.esp32c6_128k" \
  set-target esp32c6
idf.py -B build-esp32c6-128k build
```

Then build large cartridges with `--cart-ram-kib 128` (see below). Board
uploads check the limit against the firmware's `/api/runtime` response. The
setup resource screen reports `CART RAM` so the active profile can be checked
on the board.

## Matching Host Tools to the Profile

Host tools reject a cartridge whose executable memory (`mem_size`: code, data,
and `.bss`) is larger than the target's cartridge RAM window. Portable
`cartridge build` runs and QEMU staging cannot ask a running firmware for
its window, so they choose the limit as follows:

| Command | Limit used |
|---|---|
| `python3 -m prg32 cartridge build` | `--cart-ram-kib`, else 64 KiB (the default extended profile) |
| `python3 -m prg32 qemu upload` | `--cart-ram-kib`, else `CONFIG_PRG32_CART_RAM_KIB` from the `sdkconfig` next to `--flash` (normally `build-qemu/sdkconfig`), else 64 KiB |
| `python3 -m prg32 esp32c6 upload` | `cart_ram_size` reported by the board's `/api/runtime` |

`--cart-ram-kib` accepts 16 to 128, the same range as the Kconfig custom
profile. The value in `prg32/utilities/env_variables.py`
(`DEFAULT_CART_RAM_KIB`) must match the Kconfig `PRG32_CART_RAM_KIB` default.

To keep a cartridge within the classroom profile, build it with the
32 KiB limit:

```bash
python3 -m prg32 cartridge build game.S --out build/game.prg32 \
  --name game --entry-prefix game --cart-ram-kib 32
```

For a QEMU firmware built with the classroom profile, `qemu upload` picks up
32 KiB from `build-qemu/sdkconfig`; pass `--cart-ram-kib 32` when staging into
a flash image with no `sdkconfig` next to it.

## Hardware Pin Profiles

PRG32 decouples GPIO pin assignments from Kconfig defaults. Instead of hardcoding pin numbers in `components/prg32/Kconfig`, every physical hardware configuration is defined in a profile fragment within `profiles/`.

Builds attempting to compile without a pin profile are blocked at three levels:
1. **CMake**: Halts with `message(FATAL_ERROR ...)` if `CONFIG_PRG32_PINS_CONFIGURED` is unset.
2. **C Preprocessor**: Emits `#error` in `main/prg32_config.h` if compiled without a pin profile.
3. **C Runtime**: Halts firmware execution in `prg32_init()` if pins are unconfigured.

### Built-in Pin Profiles

- **`prototype1` (`profiles/sdkconfig.pins.prototype1`)**: The first prototype breadboard pinout using the ILI9341 SPI TFT display, 7-button directional controls, and MAX98357A I2S mono/stereo audio as documented in [PRG32 Hardware](../hardware/hardware.md).
- **`qemu`**: QEMU desktop builds configure virtual RGB panel and UART console input automatically via `profiles/sdkconfig.defaults.qemu`.

### Selecting a Pin Profile

Use the `--pin-profile` (or `--pins`) argument when building:

```bash
# Build with prototype 1 pins (default if omitted)
python3 -m prg32 esp32c6 build --pin-profile prototype1

# Build and flash directly
python3 -m prg32 esp32c6 build-and-flash --pin-profile prototype1

# List all available hardware pin profiles
python3 -m prg32 esp32c6 build --list-pin-profiles
```

For direct `idf.py` builds, add the profile to `SDKCONFIG_DEFAULTS`:

```bash
idf.py -B build-esp32c6 \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.pins.prototype1" \
  build
```

### Adding a New Pin Profile

Adding a new pin layout (such as a custom PCB or alternative display harness) requires no Python code modifications:

1. Create a new file in `profiles/` named `profiles/sdkconfig.pins.<name>` (e.g. `profiles/sdkconfig.pins.myboard`).
2. Set `CONFIG_PRG32_PINS_CONFIGURED=y` and define the relevant pins:
   ```ini
   CONFIG_PRG32_PINS_CONFIGURED=y
   CONFIG_PRG32_PIN_PROFILE_NAME="myboard"
   CONFIG_PRG32_PIN_LCD_MOSI=7
   CONFIG_PRG32_PIN_LCD_MISO=2
   CONFIG_PRG32_PIN_LCD_SCLK=6
   CONFIG_PRG32_PIN_LCD_CS=10
   CONFIG_PRG32_PIN_LCD_DC=8
   CONFIG_PRG32_PIN_LCD_RST=9
   CONFIG_PRG32_PIN_LCD_BL=5
   CONFIG_PRG32_PIN_BTN_UP=3
   CONFIG_PRG32_PIN_BTN_DOWN=13
   CONFIG_PRG32_PIN_BTN_LEFT=18
   CONFIG_PRG32_PIN_BTN_RIGHT=19
   CONFIG_PRG32_PIN_BTN_START=20
   CONFIG_PRG32_PIN_BTN_A=21
   CONFIG_PRG32_PIN_BTN_B=22
   CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO=4
   CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO=11
   CONFIG_PRG32_AUDIO_I2S_DATA_GPIO=23
   CONFIG_PRG32_AUDIO_I2S_SD_GPIO=-1
   CONFIG_PRG32_PIN_BUZZER=-1
   CONFIG_PRG32_PIN_RGB_LED=-1
   ```
3. Build with your profile immediately:
   ```bash
   python3 -m prg32 esp32c6 build --pin-profile myboard
   ```
