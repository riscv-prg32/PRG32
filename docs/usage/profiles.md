# PRG32 Profiles

PRG32 uses different SDK configuration profiles to adjust how the resident firmware is built. While some profiles adjust the available RAM for uploadable cartridges, others configure the framework for different hardware targets (like the QEMU emulator) or enable runtime features like metrics streaming. The active profile dictates the cartridge size limits, available drawing features, and the target platform.

## Base Profiles

- **Classroom Profile (32 KiB)**: An optional smaller window for builds that
  prioritize resident-runtime heap.
- **Extended Profile (64 KiB)**: An optional window for builds that need 64 KiB
  more free heap than the default; see [64 KiB Profile](#64-kib-profile-sdkconfigdefaultscart_64k).
- **Large Profile (128 KiB)**: The default for physical ESP32-C6 and QEMU builds.
  The stored package limit is also 128 KiB, the size of a cartridge partition.
- **QEMU Emulator Profile**: The default used by QEMU emulator builds (`profiles/sdkconfig.defaults.qemu`). It configures the system for the emulator (using `esp32c3` as the target, enabling the RGB display, UART input, and failure on real hardware) and uses the default 128 KiB cartridge RAM. Use with:
  ```bash
  idf.py -B build-qemu -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.qemu" set-target esp32c3
  ```

How the firmware keeps as much heap free as possible next to the window is
described in [Maximizing free heap](../hardware/memory.md#maximizing-free-heap).

## Optional Profiles

### 64 KiB Profile (`sdkconfig.defaults.cart_64k`)

This fragment returns to a 64 KiB window and a 64 KiB stored package limit. It
leaves 64 KiB more free heap than the default
([measured figures](../hardware/memory.md#measured-effect)) and also fits QEMU
builds.

```bash
idf.py -B build-esp32c6-64k \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.defaults.cart_64k;profiles/sdkconfig.pins.breadboard1" \
  set-target esp32c6
idf.py -B build-esp32c6-64k build
```

Board uploads check the limit against the firmware's `/api/runtime` or HELLO
response. The setup resource screen reports `CART RAM` so the active profile
can be checked on the board.

## Compatibility of Existing Cartridges

Portable cartridges do not need rebuilding when the window size changes. The
window grows upward from the same start address, `prg32_cart_exec`, and a
portable cartridge calls the firmware only through the ABI table. The firmware
checks only that `mem_size` fits the window. A cartridge built with the 32 KiB
or 64 KiB limit therefore loads unchanged on the 128 KiB default, and cartridges
already stored in the slots stay valid because the partition table is unchanged.
This was checked on 2026-10-30; see the
[validation record](../hardware/memory.md#128-kib-default-validation-2026-10-30).

The reverse does not hold: a cartridge that needs more than 64 KiB is rejected
at upload by firmware with a smaller window. To keep a new cartridge usable on
such firmware, build it with `--cart-ram-kib 64`. Legacy absolute-import
cartridges still need the exact firmware they were linked against.

An existing build directory keeps its old `sdkconfig`. `python3 -m prg32 esp32c6
build` regenerates it, but a direct `idf.py build` in an old directory stays on
64 KiB until `sdkconfig` is deleted. HELLO reports the active size.

## Matching Host Tools to the Profile

Host tools reject a cartridge whose executable memory (`mem_size`: code, data,
and `.bss`) is larger than the target's cartridge RAM window. Portable
`cartridge build` runs and QEMU staging cannot ask a running firmware for
its window, so they choose the limit as follows:

| Command | Limit used |
|---|---|
| `python3 -m prg32 cartridge build` | `--cart-ram-kib`, else 128 KiB (the default large profile) |
| `python3 -m prg32 qemu upload` | `--cart-ram-kib`, else `CONFIG_PRG32_CART_RAM_KIB` from the `sdkconfig` next to `--flash` (normally `build-qemu/sdkconfig`), else 128 KiB |
| `python3 -m prg32 esp32c6 upload` | `cart_ram_size` reported by the board's `/api/runtime` |

`--cart-ram-kib` accepts 16 to 128, the same range as the Kconfig custom
profile. The value in `prg32/utilities/env_variables.py`
(`DEFAULT_CART_RAM_KIB`) must match the Kconfig `PRG32_CART_RAM_KIB` default,
and `DEFAULT_CART_MAX_KIB` must match the `PRG32_CART_MAX_KIB` default.

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

PRG32 completely decouples GPIO pin assignments from Kconfig defaults to avoid duplication:
- **No Pin Duplication in Kconfig**: Pin numbers are not hardcoded or defaulted in `components/prg32/Kconfig`. All GPIO options in Kconfig default to `-1`.
- **Single Source of Truth**: All physical hardware pin assignments are defined exclusively in profile fragment files under `profiles/sdkconfig.pins.*`.
- **Automatic Merging**: When a named profile (like `breadboard1`) is selected, the build system automatically merges its profile file into the build configuration via `SDKCONFIG_DEFAULTS`, overriding the `-1` placeholders.
- **Custom Mode**: If and only if the `Custom Profile` mode is activated in `menuconfig`, the GPIO numbers configured manually by the user in `menuconfig` and saved in `sdkconfig` are used directly without loading an external profile file.

Builds attempting to compile physical firmware without a pin profile are blocked at three levels:
1. **CMake**: Halts with `message(FATAL_ERROR ...)` if `CONFIG_PRG32_PINS_CONFIGURED` is unset.
2. **C Preprocessor**: Emits `#error` in `main/prg32_config.h` if compiled without a pin profile.
3. **C Runtime**: Halts firmware execution in `prg32_init()` if pins are unconfigured.

### Built-in Pin Profiles

- **`breadboard1` (`profiles/sdkconfig.pins.breadboard1`)**: The first breadboard pinout using the ILI9341 SPI TFT display (DC: GPIO8, RST: GPIO9), 5-way digital navigation joystick (discrete GPIO switches), and MAX98357A I2S mono/stereo audio as documented in [Breadboard 1](../hardware/profiles/breadboard1.md).
- **`breadboard2` (`profiles/sdkconfig.pins.breadboard2`)**: The second breadboard pinout using a 2-axis analog thumbstick (read via ADC1 one-shot conversions on GPIO0 and GPIO1), updated display harness (CS: GPIO9, DC: GPIO10, RST wired to the ESP32-C6 EN pin), buttons A (GPIO18), B (GPIO19), C (GPIO11), D (GPIO3), L bumper (GPIO21), R bumper (GPIO22), and MAX98357A I2S audio (LRCLK: GPIO15) as documented in [Breadboard 2](../hardware/profiles/breadboard2.md).
- **`pcb1` (`profiles/sdkconfig.pins.pcb1`)**: The soldered PCB pinout: the `breadboard1` display, control, and audio pins, but with P1 DOWN on GPIO13 (the native USB D+ line). Like `breadboard1`, it uses the GPIO8 RGB LED shared with LCD D/C only for the boot indication. Full pinout and schematic in [PCB 1](../hardware/profiles/pcb1.md).
- **`qemu`**: QEMU desktop builds configure virtual RGB panel and UART console input automatically via `profiles/sdkconfig.defaults.qemu`.

### Selecting a Pin Profile

There are two primary ways to select a pin configuration:

#### 1. Via `idf.py menuconfig` (Interactive Dropdown)
You can set a persistent pin configuration default in your `sdkconfig` via `menuconfig`:
1. Run `idf.py menuconfig`.
2. Navigate to `Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration` -> `Hardware Pin Profile`.
3. Choose your pin profile from the dropdown:
   - **`None (no default - build halts)`**: The default setting. Pin configuration is omitted; any physical build will crash/halt at build time if unconfigured.
   - **`breadboard1 (First Breadboard)`**: Designates the reference breadboard pin profile. Pin numbers are defined in `profiles/sdkconfig.pins.breadboard1` and merged into the build, overriding the `-1` menuconfig defaults.
   - **`breadboard2 (Second Breadboard with Analog Joystick)`**: Designates the second breadboard pin profile with the ADC analog joystick and updated harness. Pin numbers are defined in `profiles/sdkconfig.pins.breadboard2`.
   - **`pcb1 (Soldered PCB, DOWN on GPIO13)`**: Designates the soldered PCB pin profile. Pin numbers are defined in `profiles/sdkconfig.pins.pcb1`.
   - **`Custom Profile (configure pins manually in menuconfig)`**: If and only if Custom profile is activated, the build uses the pin values set manually in `menuconfig` and saved in `sdkconfig`. No external profile file is appended.

#### 2. Via PRG32 Tooling (`--pin-profile`)
Pass the `--pin-profile` (or `--pins`) flag when building. If no pin profile is supplied and no Custom Profile is configured in `sdkconfig`, the build command fails fast with a helpful error.

```bash
# List available pin setup possibilities
python3 -m prg32 esp32c6 pins

# Build with breadboard 1 pins
python3 -m prg32 esp32c6 build --pin-profile breadboard1

# Build with breadboard 2 pins (analog joystick)
python3 -m prg32 esp32c6 build --pin-profile breadboard2

# Build and flash directly
python3 -m prg32 esp32c6 build --pin-profile breadboard2 && python3 -m prg32 esp32c6 flash
```

For direct `idf.py` builds, add the profile to `SDKCONFIG_DEFAULTS`:

```bash
idf.py -B build-esp32c6 \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.pins.breadboard2" \
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
   CONFIG_PRG32_PIN_BTN_DOWN=0
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

> [!NOTE]
> Adding a new profile requires **zero modifications to `Kconfig` or C code**. Pin assignments are completely decoupled and reside solely within their respective profile files in `profiles/`. Once created, your profile is immediately recognized by `python3 -m prg32 esp32c6 pins` and can be built directly with `--pin-profile <name>`.
