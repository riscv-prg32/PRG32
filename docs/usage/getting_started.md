# Getting Started With PRG32
This guide starts from an empty working directory and finishes with a small `hello_world.prg32` cartridge that can run in QEMU, upload to a physical ESP32-C6 PRG32 board, and be packaged for a Cartridge Store.

PRG32 has two related development loops:
- resident firmware development, where `idf.py` or PlatformIO builds the PRG32 runtime for the board or QEMU;
- cartridge game development, where `python3 -m prg32` links a small RISC-V assembly or C program against the portable PRG32 ABI table and produces a `.prg32` game package.

The resident firmware and cartridges must agree on the portable ABI major, hash, and required feature bits. Incompatible cartridges are rejected cleanly.
## 1. Choose The Environment
There are two environments to run PRG32: the **ESP-IDF + Tooling** environment or the **PlatformIO** environment.

The ESP-IDF + Tooling environment is the recommended one. This environment uses the provided [Python Tooling](tooling.md).

You can use PlatformIO when you want a convenient VS Code workflow for the physical ESP32-C6 firmware.  The checked-in PlatformIO environment can only be used with the physical ESP32-C6 board, not for QEMU.

| Task                                       | Recommended environment       |
| ------------------------------------------ | ----------------------------- |
| Build resident firmware for ESP32-C6       | ESP-IDF + Tooling, PlatformIO |
| Flash and monitor physical board           | ESP-IDF + Tooling, PlatformIO |
| Build QEMU firmware and run virtual screen | ESP-IDF + Tooling             |
| Build `.prg32` cartridges                  | ESP-IDF + Tooling             |
| Package/publish Cartridge Store bundles    | ESP-IDF + Tooling             |
| Modify PRG32 framework internals           | ESP-IDF + Tooling             |
## 2. Install
If you have not installed PRG32 yet, follow the Install & Setup guide in the [README](/README.md). 
## 3. Build
If you are using the **ESP-IDF + Tooling** environment, the `python -m prg32` package is available to execute commands. 

> **Important**: Remember to always source the file `export.sh` in the ESP-IDF directory you previously created before using PRG32 in a new shell. 
 
It is recommended for users to create an alias for sourcing ESP-IDF and using the PRG32 tools:
```bash
alias get_idf=". $HOME/esp-idf/export.sh"
alias prg="python -m prg32"
```
### Running PRG32 on the ESP32C6
If you have the physical PRG32 hardware, after [configuring the hardware setup](docs/hardware/hardware.md) you can build the project for the ESP32C6 and flash it to the SoC:
```bash
python -m prg32 esp32c6 build-and-flash
```
### Running PRG32 on QEMU
If you don't have the physical hardware you can still use the PRG32 framework emulated on QEMU:
```bash
python -m prg32 qemu build
```

You can run the QEMU emulator to make sure everything is working correctly:
```bash
python -m prg32 qemu run
```

> **Important**: To interact with the QEMU emulator, your active window must be **the terminal that launched QEMU**, not the QEMU virtual screen.


## 4. Navigate the Startup Menu
The first screen is the startup menu, where you can, among other things, run a cartridge, set the default boot cartridge, configure Wi-Fi, configure CartridgeStore access, browse the store and download new cartridges, open the audio setup menu, open the developer status-band menu, launch the unattended performance test, or show the about screen.

**Navigation Commands:**
- **Joystick**: Navigate menus (QEMU: keyboard arrows)
- **Button A (SET)**: Confirm selection (QEMU: keyboard 'Z')
- **Button B (RST)**: Go back (QEMU: keyboard 'X')
- **A + B + DOWN (Hold)**: Restart the PRG32 firmware
- **A + B (Hold during boot)**: Force enter setup (also happens automatically on first boot when no cartridge is stored)

Setup screens also show the active Wi-Fi mode and current IP address.

## 5. Run the Hello World Cartridge
### Create the source code for the Cartridge

Create a new local game directory outside of the PRG32 folder:
```bash
mkdir -p ../work/hello_world
```

Create the assembly source code file: 
```bash
touch ../work/hello_world/hello_world.S
```

Paste this source code inside the new file:
```asm
.option norelax
.section .text
.global hello_world_init
.global hello_world_update
.global hello_world_draw

.equ PRG32_COLOR_BLACK, 0x0000
.equ PRG32_COLOR_WHITE, 0xffff
.equ PRG32_COLOR_CYAN,  0x07ff

hello_world_init:
    addi sp, sp, -16
    sw ra, 12(sp)

    li a0, PRG32_COLOR_BLACK
    call prg32_gfx_clear

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

hello_world_update:
    ret

hello_world_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    li a0, PRG32_COLOR_BLACK
    call prg32_gfx_clear

    li a0, 32
    li a1, 40
    la a2, hello_world_title
    li a3, PRG32_COLOR_CYAN
    call prg32_gfx_text8

    li a0, 32
    li a1, 64
    la a2, hello_world_line
    li a3, PRG32_COLOR_WHITE
    call prg32_gfx_text8

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

.section .rodata
hello_world_title:
    .asciz "HELLO WORLD"
hello_world_line:
    .asciz "PRG32 cartridge from scratch"
```

The three exported symbols are the cartridge entry points:
- `hello_world_init`;
- `hello_world_update`;
- `hello_world_draw`.

### Run the Cartridge on QEMU
First you need to  build the Hello World Cartridge for QEMU.

```bash
python3 -m prg32 cartridge build \
  work/hello_world/hello_world.S \
  --portable \
  --entry-prefix hello_world \
  --name hello_world \
  --out work/hello_world/hello_world.prg32
```

Then stage the cartridge to QEMU. If QEMU is already opened, close it before running this command.
```
python3 -m prg32 qemu upload work/hello_world/hello_world.prg32
```

Then start QEMU:
```bash
python -m prg32 qemu run
```

The setup menu appears: select `Run Cartridge`, then select `cart0`. The virtual screen should show `HELLO WORLD` in the 320x200 game viewport. 
### Run the Cartridge on the ESP32C6
First you need to  build the Hello World Cartridge for the ESP32C6.

```bash
python3 -m prg32 cartridge build \
  work/hello_world/hello_world.S \
  --portable \
  --entry-prefix hello_world \
  --name hello_world \
  --out ../work/hello_world/hello_world.prg32
```

Boot up the ESP32C6 to the setup menu and make sure that your computer and the board are connected to the **same WiFi**. To do this, you can set up an Access Point on the ESP32C6, or you can connect to a local WiFi network. See the [Network](network.md) documentation file for more information.

Upload the cartridge to the board.
```
python3 -m prg32 esp32c6 upload ../work/hello_world/hello_world.prg32
```

In the setup menu two new options appear: select `Run Cartridge`, then select `cart0`. The virtual screen should show `HELLO WORLD` in the 320x200 game viewport. 

You can also run the cartridge through the python tooling: 
```bash
python -m prg32 esp32c6 --url <URL> --slot cart0
```

For any errors, before opening a Github Issue, please consult [Troubleshooting](troubleshooting.md).

## 6. What To Read Next

- For uploading your cartridge to the Cartridge Store, read the [Cartridge Store documentation file](/docs/usage/cartridge_store).
- For a complete setup verification, read the [Setup Verification documentation file](setup_verification.md).

- `docs/cartridges.md`: deeper cartridge workflow and slot behavior.
- `docs/qemu.md`: host-specific QEMU setup and troubleshooting.
- `docs/tutorial.md`: first assembly game tutorial.
- `docs/tutorial_graphic_game.md`: graphics assembly tutorial.
- `docs/tutorial_c_game.md`: C cartridge tutorial.
- `docs/api.md`: board HTTP API and Cartridge Store API reference.
- `docs/framework_manual.md`: PRG32 runtime API and ABI details.
- `docs/assets.md`: image, sprite, tile, GIF, and audio asset conversion.


