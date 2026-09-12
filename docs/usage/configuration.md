# ESP-IDF Firmware Configuration

The PRG32 framework uses the standard ESP-IDF build system. If you are a framework developer or need to tweak the core firmware settings, you can configure framework features (such as audio modes, console output, and Wi-Fi options) using the `menuconfig` tool.

To open the interactive configuration menu, run:
```bash
idf.py menuconfig
```

When you save changes in `menuconfig`, they are stored in the `sdkconfig` file located at the root of the project.

- **`sdkconfig`**: Contains your local configuration. This file is dynamically generated and represents your current build state. It is usually ignored by version control.
- **`sdkconfig.defaults`**: Contains the permanent default configurations for the repository. These defaults act as a baseline whenever a new `sdkconfig` file is generated (for example, after a fresh Git clone, or if you delete your local `sdkconfig` and run a build). Your local `sdkconfig` will override the repository defaults.
    - If you want a change (e.g., changing the fallback default audio mode) to be applied universally to all new builds, you must manually copy the relevant setting from your local `sdkconfig` into `sdkconfig.defaults`.
- **`sdkconfig.defaults.qemu`**: Contains the default configuration overrides specifically for QEMU emulator builds.

## Runtime Configuration (NVS)

While `menuconfig` and `sdkconfig.defaults` provide the baseline compile-time configuration, the PRG32 framework also allows certain system settings (such as master volume or audio output modes) to be configured dynamically by the user via the on-device Setup Menu.

These user preferences are stored in the ESP32-C6's Non-Volatile Storage (NVS) under the `prg32` namespace. At boot time, any valid setting found in NVS will take precedence and dynamically override the compile-time default. If you wipe the NVS flash or if a setting is unconfigured, the system automatically falls back to your `sdkconfig.defaults` values.
