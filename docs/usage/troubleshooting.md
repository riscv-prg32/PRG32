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
- **The prototype1 onboard RGB LED flashes green or white while the display updates**: This is expected on the fixed PCB. GPIO8 is shared by LCD D/C and the DevKitC WS2812 data input, so display traffic can resemble LED data. Firmware flashes cyan during boot and latches an off frame after display updates, but it cannot independently control both devices on one wire. Do not move the canonical LCD pin in the `prototype1` profile.
- **The serial log repeats `rmt: rmt_tx_wait_all_done(612): flush timeout`**: Firmware from 2026-10-26 until the fix of 2026-10-31 logged this twice for every display update on `prototype1`, because the LED code gave the RMT driver a wait that rounded down to zero. The LED and the display still worked. Rebuild and flash current firmware; the cost is recorded under [Onboard RGB LED](../hardware/hardware.md#onboard-rgb-led).

## QEMU Emulator Issues

- **QEMU build fails with `fatal error: <header>.h: No such file or directory` while the ESP32-C6 build succeeds**: The header's ESP-IDF component is missing from `PRG32_PRIV_REQUIRES` in `components/prg32/CMakeLists.txt` and is only reachable transitively on ESP32-C6. See [explicit component requirements](qemu.md#how-it-works).
- **Virtual screen window does not appear**: Ensure that host SDL2 libraries are properly installed (see the main `README.md`). If you are bypassing the Python tooling and running `idf.py qemu` manually, check that the `--graphics monitor` flag is included in your command.
- **QEMU runs but the game does not move**: Ensure the QEMU monitor terminal window has focus, *not* the graphical QEMU screen. The terminal captures input via the UART console. Use arrow keys or `W`/`A`/`S`/`D` for joystick 1, `Enter`/`Space` for SELECT, `J`/`Z` for A, and `K`/`X` for B.

## PlatformIO Issues

- **PlatformIO Monitor shows only ROM boot text or only its header**: The firmware was probably built with an older generated `sdkconfig.prg32-esp32c6`, or the USB monitor missed secondary-console output during reset. Delete that generated file once, rebuild, upload, open Monitor again, and press RESET/EN on the board. A healthy app boot logs `PRG32 boot: app_main entered` and the configured `prg32_lcd` pins.
- **PlatformIO says it cannot exclusively lock `/dev/cu.usbmodem...`**: Close every other Serial Monitor, ESP-IDF Monitor, Arduino Serial Monitor, and terminal using that port, then start only one PlatformIO Monitor.

## Cartridge and Partition Issues

- **Cartridge upload fails**: The emulator flash image might be missing, or the cartridge is too large. Run QEMU once to generate `build-qemu/qemu_flash.bin`, then rerun your upload command.
- **Partition mismatch errors**: Run `python3 -m prg32 doctor` and verify `profiles/partitions_prg32.csv` plus the selected cartridge slot.
- **A staged cartridge opens setup instead of starting**: Check that
  `PRG32_BOOT_SETUP_MODE` is `0` in `idf.py menuconfig` (under PRG32 Firmware Features), rebuild the firmware,
  and inspect the serial line `prg32_init: cartridge autoload failed`. A
  Store-ready package should load without copying its metadata or artwork into
  temporary RAM; an out-of-memory error here indicates an older firmware build.
- **A cartridge written directly to `cart0` over browser USB opens setup**: ROM
  flashing replaces the slot bytes but cannot update the firmware's saved
  default-slot setting. When more than one valid slot exists and no default is
  saved, choose `cart0` in setup and use the default-cartridge command. With
  only one valid cartridge installed, firmware autoloads it.
- **The console restarts whenever a USB tool connects**: the tool changes DTR
  and RTS one after the other, which the ESP32-C6 USB Serial/JTAG treats as a
  reset request. The PRG32 SDK leaves both lines as the operating system sets
  them; other tools must do the same or change both lines in one call. See
  [modem-control lines](../software/usb_protocol.md#modem-control-lines).
- **The USB upload screen stays visible after the host stopped**: firmware
  cancels an upload that receives no frame for 10 seconds, shows
  `UPLOAD TIMED OUT`, erases the incomplete slot and returns to setup. If the
  screen persists longer, the firmware predates the October 2026 protocol
  revision; flash the current firmware once with the ROM flasher.
- **`PRG32 device did not answer protocol request 1`**: another program holds
  the serial port (close monitors), the cable is charge-only, or the board runs
  firmware without the device protocol. Native USB uploads use the ESP32-C6
  connector labelled USB, not the UART bridge connector.
