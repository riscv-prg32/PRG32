# Deployment Guide

## Requirements

- ESP-IDF installed
- ESP32-C6 board
- ILI9341 SPI TFT display
- Buttons wired to GPIO with pull-ups
- Passive buzzer

For desktop screen testing instead of real hardware:

- ESP-IDF installed
- `qemu-riscv32` installed through `idf_tools.py`
- SDL2 and related QEMU runtime libraries on Linux/macOS

## Build and flash

```bash
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor
```

Use `build-esp32c6` for real boards. Do not flash a `build-qemu` image to
hardware; that build targets ESP32-C3 and selects the virtual QEMU RGB panel.
On a healthy physical build the serial monitor logs `prg32_lcd` with the
ILI9341 pins before the splash screen is drawn.

## Default app

The default firmware is a resident PRG32 runtime. On the physical ESP32-C6
build it shows the full 320x240 splash screen, samples the joystick, and enters
setup mode when A and B are held or when no cartridge is stored. If one
cartridge is stored, it starts automatically. If both `cart0` and `cart1`
contain cartridges, setup lets the user run one immediately or save a default
boot cartridge.

In setup mode, choose access-point mode for the standard cartridge upload
workflow, or infrastructure mode to scan nearby SSIDs and connect the board to
an existing network. The same setup menu can launch the developer band menu,
the about screen, and the device demo. With no cartridge installed, the
firmware starts the upload AP and waits in setup.

## Flash once, upload games

The physical build can start a Wi-Fi AP for cartridge upload:

```text
SSID: PRG32
Password: prg32game
URL: http://192.168.4.1
```

Build and upload a game:

```bash
python3 tools/prg32_game.py build \
  examples/games/asteroids/graphics/game.S \
  --firmware-elf build-esp32c6/PRG32.elf \
  --entry-prefix asteroids_graphics \
  --name asteroids \
  --out build-esp32c6/asteroids.prg32

python3 tools/prg32_game.py upload build-esp32c6/asteroids.prg32 --url http://192.168.4.1
```

Use `--slot cart1` to upload a second cartridge:

```bash
python3 tools/prg32_game.py upload build-esp32c6/asteroids.prg32 --slot cart1 --url http://192.168.4.1
```

See `docs/cartridges.md`.

## Build an example game

Example games live outside the default app in `examples/games`. During labs,
add one source file to `main/CMakeLists.txt` and call its exported symbols from
`main/main.c`.

Example game builds may still use `main/prg32_config.h` for display mode, pins,
and Wi-Fi credentials:

```c
#define PRG32_DEFAULT_MODE PRG32_MODE_UART_LCD_MIRROR
```

Pin assignments in `prg32_config.h` are reference values. Check your ESP32-C6 board pinout before wiring.

## Run in QEMU with a virtual screen

QEMU uses a separate build directory and `sdkconfig.defaults.qemu` so the
physical board build remains unchanged. The emulator target is ESP32-C3 because
that is Espressif's documented RISC-V QEMU graphics target; PRG32 hardware
deployment still targets ESP32-C6.

```bash
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

The QEMU build selects `CONFIG_PRG32_DISPLAY_QEMU_RGB` and renders the PRG32
320x240 screen with Espressif's virtual RGB panel. Game APIs remain centered in
the 320x200 viewport. The normal hardware build keeps the ESP32-C6 target and
ILI9341 SPI display backend.

QEMU cartridge testing uses the same `.prg32` package but stages it into
`build-qemu/qemu_flash.bin`:

```bash
python3 tools/prg32_game.py upload-qemu build-qemu/asteroids.prg32
```

Shortcut scripts:

- macOS/Linux: `tools/qemu.sh`
- Windows PowerShell: `tools/qemu.ps1`

See `docs/qemu.md` for host-specific setup and troubleshooting.
