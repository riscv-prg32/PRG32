# Troubleshooting

Here are common issues and solutions when working with the PRG32 framework.

- `idf.py: command not found`: ESP-IDF is not sourced. Run `. $HOME/esp-idf/export.sh`.
- **Black display on real ESP32-C6 hardware**: Rebuild with the physical build directory, not the QEMU one:
  `idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6`
  then
  `idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor`.
  The monitor should log `prg32_lcd` with the configured ILI9341 pins.
- **PlatformIO Monitor shows only ROM boot text or only its header**: The firmware was probably built with an older generated `sdkconfig.prg32-esp32c6`, or the USB monitor missed secondary-console output during reset. Delete that generated file once, rebuild, upload, open Monitor again, and press RESET/EN on the board. A healthy app boot logs `PRG32 boot: app_main entered` and the configured `prg32_lcd` pins.
- **PlatformIO says it cannot exclusively lock `/dev/cu.usbmodem...`**: Close every other Serial Monitor, ESP-IDF Monitor, Arduino Serial Monitor, and terminal using that port, then start only one PlatformIO Monitor.
- **QEMU runs but the game does not move**: Focus the QEMU monitor terminal and use arrow keys or `W`/`A`/`S`/`D` for joystick 1, `Enter`/`Space` for SELECT, `J`/`Z` for A, and `K`/`X` for B.
- **Cartridge upload fails**: `build-qemu/qemu_flash.bin` is missing/invalid, or the cartridge is too large. Run QEMU once, then rerun `upload-qemu`.
- **`riscv32-esp-elf-gcc` missing**: ESP-IDF toolchain not installed/sourced. Re-run `./install.sh esp32c3,esp32c6` and source the ESP-IDF export script.
- **Partition mismatch errors**: Run `python3 -m prg32 doctor` and verify `partitions_prg32.csv` plus the selected cartridge slot.
