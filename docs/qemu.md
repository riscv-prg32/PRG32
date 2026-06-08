# PRG32 QEMU Screen Emulator

PRG32 can run on a desktop with Espressif QEMU and show the 320x240 PRG32
screen in a virtual RGB framebuffer window. Framework screens use the full
resolution; game code still draws into the centered 320x200 viewport. This is
intended for students who want to compile and test graphics before flashing real
ESP32-C6 hardware.

The QEMU build uses ESP32-C3 as the emulator target because Espressif documents
the maintained RISC-V QEMU graphics path for ESP32-C3. PRG32 student games still
use the same 32-bit RISC-V calling convention and PRG32 ABI, and the physical
hardware build remains ESP32-C6.

The assembly ABI does not change. A game still calls:

- `prg32_gfx_clear`
- `prg32_gfx_pixel`
- `prg32_gfx_rect`
- `prg32_gfx_text8`
- `prg32_gfx_present`
- `prg32_playfield_draw_dual`
- `prg32_sprite_draw_frame`

Only the display backend changes.

## Supported Hosts

Espressif provides QEMU RISC-V packages for:

- macOS Intel
- macOS Apple Silicon
- Windows x64
- Linux x86_64
- Linux arm64

Use ESP-IDF 5.3 or newer; the `esp_lcd_qemu_rgb` component declares that
minimum IDF version. Install ESP-IDF first, then install QEMU:

```bash
python $IDF_PATH/tools/idf_tools.py install qemu-riscv32
```

After installing QEMU, reactivate ESP-IDF:

```bash
. $IDF_PATH/export.sh
```

On Windows, use the ESP-IDF PowerShell or Command Prompt shortcut, or run the
matching ESP-IDF export script from your ESP-IDF installation.

## Linux and macOS Runtime Libraries

Linux and macOS hosts also need the native libraries used by the QEMU window.

Ubuntu or Debian:

```bash
sudo apt-get install -y libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0
```

macOS with Homebrew:

```bash
brew install libgcrypt glib pixman sdl2 libslirp
```

## First QEMU Run

Use a separate build directory so the physical board configuration and the QEMU
configuration do not overwrite each other.

On Windows:
```bash
cd <path_to_PRG32>
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

On Linux or MacOS:
```bash
cd <path_to_PRG32>
./scripts/qemu/build_qemu.sh
```

The second command builds the firmware if needed, starts QEMU, opens the virtual
screen window, and attaches the serial monitor.

Shortcut scripts are also provided:

```bash
tools/qemu.sh
```

Windows PowerShell from an ESP-IDF shell:

```powershell
tools/qemu.ps1
```

## VS Code Tasks

Open `PRG32.code-workspace` and run these tasks:

1. `PRG32: qemu set target esp32c3`
2. `PRG32: qemu build`
3. `PRG32: qemu screen`

For debugger exercises, use two terminals:

1. Run `PRG32: qemu debug server`.
2. Run `PRG32: qemu gdb`.

Then set breakpoints in symbols such as `pong_graphics_update` or
`asteroids_graphics_draw`.

## How It Works

`components/prg32/Kconfig` selects one display backend:

- `CONFIG_PRG32_DISPLAY_ILI9341`: physical ILI9341 SPI TFT.
- `CONFIG_PRG32_DISPLAY_QEMU_RGB`: Espressif QEMU virtual RGB panel.

The QEMU build uses `sdkconfig.defaults.qemu`, which enables
`CONFIG_PRG32_DISPLAY_QEMU_RGB`. The component manifest
`components/prg32/idf_component.yml` pulls in `espressif/esp_lcd_qemu_rgb` only
for the ESP32-C3 emulator target.

The physical firmware still uses the ILI9341 backend by default.

The QEMU display backend exposes the same normalized framebuffer snapshot helper
used by the physical ILI9341 backend. The resident HTTP endpoint
`/api/screenshot.bmp` therefore produces the same 320x240 BMP format whenever
the firmware HTTP server is reachable in the chosen run configuration.

## Cartridges in QEMU

QEMU uses the same uploadable `.prg32` game package as the real board, but QEMU
does not emulate the classroom Wi-Fi AP. Stage the cartridge into the emulator
flash image before starting QEMU:

On Windows:
```bash
python3 tools/prg32_game.py build \
  examples/games/asteroids/graphics/game.S \
  --firmware-elf build-qemu/PRG32.elf \
  --entry-prefix asteroids_graphics \
  --name asteroids \
  --out build-qemu/asteroids.prg32
```
If `build-qemu/flash_image.bin` does not exist yet, start QEMU once so ESP-IDF
generates the flash image, quit QEMU, then run:

```bash
python3 tools/prg32_game.py upload-qemu build-qemu/asteroids.prg32
```

Then run `PRG32: qemu screen`.

On Linux or MacOS:

If `build-qemu/flash_image.bin` does not exist yet, start QEMU once so ESP-IDF
generates the flash image:

```bash
cd <path_to_PRG32>
./scripts/qemu/build_qemu.sh
```

Then stage the cartridge:
```bash
cd <path_to_PRG32>
./scripts/qemu/qemu_inject_cartridge.sh <path_to_cartridge.prg32>
./scripts/qemu/launch_qemu.sh
```

## Wiring the game into QEMU

Example games stay outside the default app. To test one in QEMU, you can wire it into
`main/CMakeLists.txt` and `main/main.c` exactly as you would for the real board,
then run:

On Windows:
```bash
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

On Linux or MacOS:
```bash
cd <path_to_PRG32>
./scripts/qemu/build_qemu.sh
./scripts/qemu/lauch_qemu.sh
```

The same source can later be built for the physical board:

```bash
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor
```

## Input in QEMU

QEMU screen emulation validates rendering, frame timing, and most assembly
debugging exercises. The QEMU defaults disable physical GPIO buttons and the
buzzer, but the QEMU RGB build enables a small UART-console keyboard mapper for
player 1 input.

When the QEMU monitor terminal has focus, use these keys:

| PRG32 input | QEMU key |
| --- | --- |
| Joystick 1 LEFT / RIGHT / UP / DOWN | arrow keys or `A` / `D` / `W` / `S` |
| SELECT | `Enter` or `Space` |
| A button | `J` or `Z` |
| B button | `K`, `X`, `Backspace`, or `Esc` |

The PRG32 input ABI remains the same:

```text
bit 0 LEFT, bit 1 RIGHT, bit 2 UP, bit 3 DOWN, bit 4 A, bit 5 B, bit 6 SELECT
```

Framework code can also call `prg32_diag_set_input_state()` to inject the same
player-1 bits in QEMU-oriented tests.

The multiplayer API is available in QEMU without real Wi-Fi. A cartridge can
call `prg32_multiplayer_join()` with the same signature it uses on hardware;
the call succeeds locally, `prg32_multiplayer_available()` returns true, and
peer snapshots are empty by default.

## Troubleshooting

If `idf.py qemu --graphics monitor` says `qemu-system-riscv32` is missing,
install the QEMU tool and reactivate ESP-IDF:

```bash
python $IDF_PATH/tools/idf_tools.py install qemu-riscv32
. $IDF_PATH/export.sh
```

If the virtual screen window does not appear, confirm that `--graphics` is in the
command and that the host SDL2 libraries are installed.

If a real board flash fails, crashes during display initialization, or shows a
black display with no splash screen, check that you did not use the `build-qemu`
directory. The QEMU build targets ESP32-C3 and uses a virtual panel that does
not exist on real PRG32 hardware. For the ESP32-C6 board, rebuild with:

```bash
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor
```

## References

- [ESP-IDF QEMU Emulator for ESP32-C3][esp-idf-qemu]
- [Espressif `esp_lcd_qemu_rgb` component](https://components.espressif.com/components/espressif/esp_lcd_qemu_rgb)
- [IDF Component Manager manifest reference][idf-component-manifest]

[esp-idf-qemu]: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/tools/qemu.html
[idf-component-manifest]: https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html
