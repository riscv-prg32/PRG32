# Troubleshooting

Here are common issues and solutions when working with the PRG32 framework.

## Toolchain and Environment Issues

- **`idf.py: command not found`**: ESP-IDF is not sourced. Run `. $HOME/esp-idf/export.sh`.
- **`riscv32-esp-elf-gcc` missing**: ESP-IDF toolchain not installed/sourced. Re-run `./install.sh esp32c3,esp32c6` and source the ESP-IDF export script.
- **Error: `qemu-system-riscv32` is missing**: Install the QEMU tool and reactivate ESP-IDF:
  ```bash
  python $IDF_PATH/tools/idf_tools.py install qemu-riscv32
  . $IDF_PATH/export.sh
  ```

## Hardware and Flashing Issues

- **Serial port not detected**: If the serial port is not detected, pass it explicitly. Add `-p COMx` on Windows, `-p /dev/ttyACM0` on Linux, or `-p /dev/cu.usbmodemXXXX` on macOS when using raw `idf.py` commands.
- **Physical board flashes fail, crash, or show a black display**: You likely compiled the firmware for QEMU (`ESP32-C3`) instead of the physical board (`ESP32-C6`). Rebuild and flash the physical board using the Python tooling:
  ```bash
  python3 -m prg32 esp32c6 build-and-flash
  ```
  *(The monitor should log `prg32_lcd` with the configured ILI9341 pins if successful.)*

## QEMU Emulator Issues

- **Virtual screen window does not appear**: Ensure that host SDL2 libraries are properly installed (see the main `README.md`). If you are bypassing the Python tooling and running `idf.py qemu` manually, check that the `--graphics monitor` flag is included in your command.
- **QEMU runs but the game does not move**: Ensure the QEMU monitor terminal window has focus, *not* the graphical QEMU screen. The terminal captures input via the UART console. Use arrow keys or `W`/`A`/`S`/`D` for joystick 1, `Enter`/`Space` for SELECT, `J`/`Z` for A, and `K`/`X` for B.

## PlatformIO Issues

- **PlatformIO Monitor shows only ROM boot text or only its header**: The firmware was probably built with an older generated `sdkconfig.prg32-esp32c6`, or the USB monitor missed secondary-console output during reset. Delete that generated file once, rebuild, upload, open Monitor again, and press RESET/EN on the board. A healthy app boot logs `PRG32 boot: app_main entered` and the configured `prg32_lcd` pins.
- **PlatformIO says it cannot exclusively lock `/dev/cu.usbmodem...`**: Close every other Serial Monitor, ESP-IDF Monitor, Arduino Serial Monitor, and terminal using that port, then start only one PlatformIO Monitor.

## Cartridge and Partition Issues

- **Cartridge upload fails**: The emulator flash image might be missing, or the cartridge is too large. Run QEMU once to generate `build-qemu/qemu_flash.bin`, then rerun your upload command.
- **Partition mismatch errors**: Run `python3 -m prg32 doctor` and verify `partitions_prg32.csv` plus the selected cartridge slot.
