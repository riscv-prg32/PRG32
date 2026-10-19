# ESP-IDF Firmware Configuration

The PRG32 framework uses the standard ESP-IDF build system. If you are a framework developer or need to tweak the core firmware settings, you can configure framework features (such as audio modes, console output, and Wi-Fi options) using the `menuconfig` tool.

To open the interactive configuration menu, run:
```bash
idf.py menuconfig
```

When you save changes in `menuconfig`, they are stored in the `sdkconfig` file located at the root of the project.

- **`sdkconfig`**: Contains the active build configuration. This file is dynamically generated in the project root and target build directory (`build-esp32c6/sdkconfig` or `build-qemu/sdkconfig`). It is ignored by version control.
- **`profiles/sdkconfig.defaults`**: Contains the permanent default configurations for the repository. These defaults act as a baseline whenever a new `sdkconfig` file is generated (for example, after a fresh Git clone, or if you delete your local `sdkconfig` and run a build).
    - If you want a change (e.g., changing the fallback default audio mode) to be applied universally to all new builds, you must manually copy the relevant setting from your local `sdkconfig` into `profiles/sdkconfig.defaults`.
- **`profiles/sdkconfig.defaults.qemu`**: Contains the default configuration overrides specifically for QEMU emulator builds.
- **`profiles/sdkconfig.defaults.local`**: Contains developer-specific local overrides (e.g., Wi-Fi credentials, debugging flags, or local pin profile selections) that are ignored by version control. Local configurations always prevail over base repository and target defaults.
- **`profiles/sdkconfig.pins.*`**: Hardware pin configuration profiles (e.g. `profiles/sdkconfig.pins.prototype1`, `profiles/sdkconfig.pins.prototype2`). GPIO pins are decoupled from hardcoded Kconfig defaults and must be provided via an explicit pin profile (see [PRG32 Profiles](profiles.md#hardware-pin-profiles)).

### Configuration Precedence & Local Overrides

PRG32 supports developer-specific settings via `profiles/sdkconfig.defaults.local`. To ensure local changes always take effect without being overridden by base repository defaults, configuration files are evaluated in the following order of precedence (later files take precedence over earlier ones):

1. Base defaults: `profiles/sdkconfig.defaults`
2. Target defaults: `profiles/sdkconfig.defaults.esp32c6` or `profiles/sdkconfig.defaults.qemu`
3. Hardware pin profile: `profiles/sdkconfig.pins.<profile>` (if selected)
4. Local overrides: `profiles/sdkconfig.defaults.local` (**always prevails**)

To prevent stale cached settings in `sdkconfig` from interfering with local overrides, PRG32 firmware build operations (`python3 -m prg32 esp32c6 build`, `python3 -m prg32 qemu build`, `python3 -m prg32 esp32c6 prepare-firmware`) and reset commands (`python3 -m prg32 esp32c6 reset`) automatically erase `./sdkconfig` and the target-specific sdkconfig (`build-esp32c6/sdkconfig` or `build-qemu/sdkconfig`) prior to building.

If you have manually tweaked settings via `idf.py menuconfig` and wish to preserve your current `sdkconfig` without having it erased on build or reset, pass the `-k` / `--keep-sdkconfig` (or `--no-erase-sdkconfig`) option:
```bash
# Build while preserving existing manual sdkconfig adjustments
python3 -m prg32 esp32c6 build --keep-sdkconfig --skip-target
```

### Hardware Pin Profile Selection in Menuconfig

Under `Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration`:
- **Hardware Pin Profile**: A dropdown choice menu that lets you choose the pin configuration:
  - `None (no default - build halts)`: **(Default)** Leaves pin assignments unconfigured (`-1`). Building physical firmware without selecting a profile will crash/halt the build with a descriptive error.
  - `prototype1 (First Prototype Breadboard)`: Designates the reference breadboard pin profile. The GPIO numbers are defined in `profiles/sdkconfig.pins.prototype1` and merged during the build (pin fields in menuconfig remain `-1` and are overridden by the profile).
  - `prototype2 (Second Prototype Breadboard with Analog Joystick)`: Designates the second breadboard pin profile with analog thumbstick on GPIO0/1 and updated display wiring in `profiles/sdkconfig.pins.prototype2`.
  - `Custom Profile (configure pins manually in menuconfig)`: If and only if Custom profile is activated, the build uses the GPIO pin numbers configured manually in `menuconfig` and saved in `sdkconfig`. No external profile file is appended.

You can also view available pin profiles from the CLI using:
```bash
python3 -m prg32 esp32c6 pins
```

## Runtime Configuration (NVS)

While `menuconfig` and `profiles/sdkconfig.defaults` provide the baseline compile-time configuration, the PRG32 framework also allows certain system settings (such as master volume or audio output modes) to be configured dynamically by the user via the on-device Setup Menu.

These user preferences are stored in the ESP32-C6's Non-Volatile Storage (NVS) under the `prg32` namespace. At boot time, any valid setting found in NVS will take precedence and dynamically override the compile-time default. If you wipe the NVS flash or if a setting is unconfigured, the system automatically falls back to your `profiles/sdkconfig.defaults` values.
