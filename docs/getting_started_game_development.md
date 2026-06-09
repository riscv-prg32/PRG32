# Getting Started With PRG32 Game Development

This guide starts from an empty working directory and finishes with a small
`hello_world.prg32` cartridge that can run in QEMU, upload to a physical
ESP32-C6 PRG32 board, and be packaged for a Cartridge Store.

PRG32 has two related development loops:

- resident firmware development, where `idf.py` or PlatformIO builds the PRG32
  runtime for the board or QEMU;
- cartridge game development, where `tools/prg32_game.py` links a small RISC-V
  assembly or C program against a resident firmware ELF and produces a
  `.prg32` game package.

The resident firmware must be rebuilt when the runtime ABI changes. Cartridges
must then be rebuilt against the matching `PRG32.elf` so function addresses and
the cartridge RAM address stay correct.

## 1. Choose The Environment

Use ESP-IDF when you need the complete PRG32 workflow, QEMU, firmware changes,
or classroom reproducibility.

Use PlatformIO when you want a convenient VS Code workflow for the physical
ESP32-C6 firmware. The checked-in PlatformIO environment targets the real
ESP32-C6 board. Use ESP-IDF commands for QEMU.

Recommended combinations:

| Task | Recommended environment |
|---|---|
| Build resident firmware for ESP32-C6 | ESP-IDF or PlatformIO |
| Flash and monitor physical board | ESP-IDF or PlatformIO |
| Build QEMU firmware and run virtual screen | ESP-IDF |
| Build `.prg32` cartridges | Python plus ESP-IDF RISC-V toolchain |
| Package/publish Cartridge Store bundles | Python, `zip`, and `curl` |
| Modify PRG32 framework internals | ESP-IDF |

## 2. Install Common Tools

All platforms need:

- Git;
- Python 3;
- CMake;
- Ninja;
- an ESP-IDF 5.3 or newer toolchain with `esp32c3` and `esp32c6` installed;
- `riscv32-esp-elf-gcc`, normally supplied by ESP-IDF;
- a terminal where ESP-IDF has been exported before running `idf.py` or the
  cartridge builder.

Useful validation commands:

```bash
git --version
python3 --version
cmake --version
ninja --version
idf.py --version
riscv32-esp-elf-gcc --version
python3 tools/prg32_game.py doctor
```

If `idf.py` or `riscv32-esp-elf-gcc` is missing, the usual fix is to install the
ESP-IDF tools for both targets and source the ESP-IDF export script again.

## 3. Windows Setup

### Windows With ESP-IDF

1. Install Git for Windows.
2. Install Visual Studio Code.
3. Install the Espressif ESP-IDF extension if you want IDE integration.
4. Download and run the Espressif ESP-IDF Tools Installer for Windows.
5. Select ESP-IDF 5.3 or newer.
6. Include both `esp32c3` and `esp32c6` tool support.
7. Open the ESP-IDF PowerShell shortcut created by the installer.

Check the tools:

```powershell
git --version
python --version
idf.py --version
riscv32-esp-elf-gcc --version
```

Use the ESP-IDF PowerShell for PRG32 commands. A normal PowerShell usually does
not have the ESP-IDF environment on `PATH`.

### Windows With PlatformIO

1. Install Git for Windows.
2. Install Visual Studio Code.
3. Install the PlatformIO extension.
4. Install the Microsoft C/C++ extension.
5. Install the Python extension.
6. Open `PRG32.code-workspace`.

PlatformIO builds the physical ESP32-C6 firmware:

```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```

For QEMU, use ESP-IDF PowerShell and the QEMU commands later in this guide.

Windows serial notes:

- ESP32-C6 boards usually appear as `COMx`.
- If flashing fails, pass the port explicitly, for example `-p COM5`.
- Close Arduino Serial Monitor, PlatformIO Monitor, and ESP-IDF Monitor before
  opening a new monitor on the same port.

## 4. Linux Setup

These commands target Debian and Ubuntu. Install equivalent packages on Fedora,
Arch, and other distributions.

```bash
sudo apt update
sudo apt install -y \
  git wget flex bison gperf python3 python3-venv python3-pip \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
  libusb-1.0-0 curl zip
```

Install ESP-IDF:

```bash
cd "$HOME"
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3,esp32c6
. ./export.sh
```

Serial permission:

```bash
sudo usermod -aG dialout "$USER"
```

Log out and back in after changing groups. ESP32-C6 boards often appear as
`/dev/ttyACM0`; USB UART bridges often appear as `/dev/ttyUSB0`.

Optional PlatformIO CLI:

```bash
python3 -m venv "$HOME/.venv-platformio"
. "$HOME/.venv-platformio/bin/activate"
python3 -m pip install platformio
```

## 5. MacOS Setup

Install Homebrew first if it is not already available, then install host tools:

```bash
brew install git cmake ninja dfu-util ccache libusb python curl zip
```

Install ESP-IDF:

```bash
cd "$HOME"
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3,esp32c6
. ./export.sh
```

Useful shell alias:

```bash
alias get_idf=". $HOME/esp-idf/export.sh"
```

Run `get_idf` in each new terminal before using `idf.py`,
`riscv32-esp-elf-gcc`, or `tools/prg32_game.py build`.

Optional PlatformIO CLI:

```bash
python3 -m venv "$HOME/.venv-platformio"
. "$HOME/.venv-platformio/bin/activate"
python3 -m pip install platformio
```

## 6. Clone PRG32

Start from a working directory that contains no PRG32 checkout yet.

Linux/macOS:

```bash
mkdir -p "$HOME/prg32-work"
cd "$HOME/prg32-work"
git clone https://github.com/raffmont/PRG32.git
cd PRG32
. "$HOME/esp-idf/export.sh"
python3 tools/prg32_game.py doctor
```

Windows ESP-IDF PowerShell:

```powershell
mkdir $HOME\prg32-work
cd $HOME\prg32-work
git clone https://github.com/raffmont/PRG32.git
cd PRG32
python tools\prg32_game.py doctor
```

If the project is already cloned, start in the repository root instead.

## 7. Build The Resident Firmware For QEMU

QEMU uses the Espressif ESP32-C3 RISC-V emulator target and the PRG32 virtual
RGB display backend. The physical board remains ESP32-C6.

Linux/macOS:

```bash
idf.py -B build-qemu \
  -D SDKCONFIG=build-qemu/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu \
  set-target esp32c3

idf.py -B build-qemu \
  -D SDKCONFIG=build-qemu/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu \
  build
```

Windows ESP-IDF PowerShell:

```powershell
idf.py -B build-qemu `
  -D SDKCONFIG=build-qemu\sdkconfig `
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu `
  set-target esp32c3

idf.py -B build-qemu `
  -D SDKCONFIG=build-qemu\sdkconfig `
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu `
  build
```

Checkpoint:

```bash
ls build-qemu/PRG32.elf
```

The cartridge builder reads this ELF file to find the PRG32 runtime ABI.

## 8. Create A Hello World Cartridge From Scratch

Create a new local game directory outside the standard examples:

```bash
mkdir -p work/hello_world
```

Create `work/hello_world/hello_world.S` with this source:

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

Every call into PRG32 C helpers saves and restores `ra`, and the stack remains
16-byte aligned around calls.

## 9. Build The Hello World Cartridge For QEMU

```bash
python3 tools/prg32_game.py build \
  work/hello_world/hello_world.S \
  --firmware-elf build-qemu/PRG32.elf \
  --entry-prefix hello_world \
  --name hello_world \
  --out build-qemu/hello_world.prg32
```

Checkpoint:

```bash
ls -lh build-qemu/hello_world.prg32
```

If this fails with `missing tool: riscv32-esp-elf-gcc`, source ESP-IDF again.

## 10. Stage And Run The Cartridge In QEMU

First start QEMU once so ESP-IDF creates `build-qemu/qemu_flash.bin`:

```bash
idf.py -B build-qemu \
  -D SDKCONFIG=build-qemu/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu \
  qemu --graphics monitor
```

Quit QEMU with `Ctrl+]`, then stage the cartridge:

```bash
python3 tools/prg32_game.py upload-qemu \
  build-qemu/hello_world.prg32 \
  --flash build-qemu/qemu_flash.bin
```

Start QEMU again:

```bash
idf.py -B build-qemu \
  -D SDKCONFIG=build-qemu/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu \
  qemu --graphics monitor
```

The virtual screen should show `HELLO WORLD` in the 320x200 game viewport. If
the setup menu appears instead, use the setup menu to run `cart0`, or stage the
cartridge again after QEMU creates a fresh flash image.

## 11. Build And Flash The Resident Firmware For ESP32-C6

Use a separate build directory for the physical board:

Linux/macOS:

```bash
idf.py -B build-esp32c6 \
  -D SDKCONFIG=build-esp32c6/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults \
  set-target esp32c6

idf.py -B build-esp32c6 \
  -D SDKCONFIG=build-esp32c6/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults \
  build

idf.py -B build-esp32c6 \
  -D SDKCONFIG=build-esp32c6/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults \
  flash monitor
```

Windows ESP-IDF PowerShell:

```powershell
idf.py -B build-esp32c6 `
  -D SDKCONFIG=build-esp32c6\sdkconfig `
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults `
  set-target esp32c6

idf.py -B build-esp32c6 `
  -D SDKCONFIG=build-esp32c6\sdkconfig `
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults `
  build

idf.py -B build-esp32c6 `
  -D SDKCONFIG=build-esp32c6\sdkconfig `
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults `
  flash monitor
```

If the serial port is not detected, add `-p COM5` on Windows,
`-p /dev/ttyACM0` on Linux, or `-p /dev/cu.usbmodemXXXX` on macOS.

PlatformIO physical build alternative:

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

The board should show the PRG32 splash and then setup if no cartridge is stored.

## 12. Build The Physical ESP32-C6 Cartridge

Build the same source against the physical firmware ELF:

```bash
python3 tools/prg32_game.py build \
  work/hello_world/hello_world.S \
  --firmware-elf build-esp32c6/PRG32.elf \
  --entry-prefix hello_world \
  --name hello_world \
  --out build-esp32c6/hello_world.prg32
```

Do not upload the QEMU cartridge to the ESP32-C6 board. QEMU and hardware use
the same package format, but each cartridge must be linked against the matching
resident firmware ELF.

## 13. Upload The Cartridge To The Physical Board

On the board, enter setup mode and start the PRG32 access point. The default
classroom values are:

```text
SSID:     PRG32
Password: prg32game
URL:      http://192.168.4.1
```

Connect the development computer to the `PRG32` Wi-Fi network, then upload:

```bash
python3 tools/prg32_game.py upload \
  build-esp32c6/hello_world.prg32 \
  --url http://192.168.4.1
```

Upload to the second slot with:

```bash
python3 tools/prg32_game.py upload \
  build-esp32c6/hello_world.prg32 \
  --slot cart1 \
  --url http://192.168.4.1
```

The firmware stores the cartridge and can run it immediately. If both slots
contain cartridges, use setup to run a slot or save a default cartridge.

Useful runtime checks:

```bash
python3 tools/prg32_game.py runtime --url http://192.168.4.1
curl http://192.168.4.1/api/games
curl http://192.168.4.1/api/screenshot.bmp --output hello_world.bmp
```

## 14. Create A Cartridge Store Publishing Package

The current checked-in cartridge tool builds and uploads board/QEMU cartridges.
For store publishing, create the metadata bundle explicitly. Cartridge Store
accepts this zip at `POST /api/publish/bundle`; `POST /api/publish` is a
compatibility alias for the same zip-bundle shape. Check the store
administrator's token and editor-review policy.

Create a bundle directory:

```bash
mkdir -p build/store/hello_world
cp build-esp32c6/hello_world.prg32 \
  build/store/hello_world/hello_world-esp32c6.prg32
cp build-qemu/hello_world.prg32 \
  build/store/hello_world/hello_world-qemu.prg32
```

Create `build/store/hello_world/manifest.json`:

```json
{
  "abi": "prg32-metadata-1.0",
  "id": "org.uniparthenope.hello-world",
  "title": "Hello World",
  "version": "1.0.0",
  "summary": "Minimal PRG32 hello world cartridge.",
  "authors": [
    {
      "name": "Your Name",
      "affiliation": "Your Course Or Lab"
    }
  ],
  "tags": ["example", "assembly", "hello-world"],
  "architectures": [
    {
      "id": "esp32c6",
      "file": "hello_world-esp32c6.prg32"
    },
    {
      "id": "qemu",
      "file": "hello_world-qemu.prg32"
    }
  ]
}
```

Package it:

```bash
cd build/store/hello_world
zip -r ../hello_world-1.0.0.zip manifest.json \
  hello_world-esp32c6.prg32 \
  hello_world-qemu.prg32
cd ../../..
```

Checkpoint:

```bash
unzip -l build/store/hello_world-1.0.0.zip
```

## 15. Publish The Package

Set the store URL and, if required, the publishing token:

Linux/macOS:

```bash
export PRG32_STORE_URL=http://192.168.1.42:5080
export PRG32_STORE_TOKEN=replace-with-classroom-token
```

Windows PowerShell:

```powershell
$env:PRG32_STORE_URL = "http://192.168.1.42:5080"
$env:PRG32_STORE_TOKEN = "replace-with-classroom-token"
```

Publish with `curl`:

```bash
curl -X POST "$PRG32_STORE_URL/api/publish/bundle" \
  -H "Authorization: Bearer $PRG32_STORE_TOKEN" \
  -F "bundle=@build/store/hello_world-1.0.0.zip"
```

The compatibility alias accepts the same bundle:

```bash
curl -X POST "$PRG32_STORE_URL/api/publish" \
  -H "Authorization: Bearer $PRG32_STORE_TOKEN" \
  -F "bundle=@build/store/hello_world-1.0.0.zip"
```

If the store does not require authentication, omit the `Authorization` header.

Verify the catalog:

```bash
curl "$PRG32_STORE_URL/api/games"
curl "$PRG32_STORE_URL/api/games/org.uniparthenope.hello-world"
```

If the upload response says `status: pending`, an editor must verify the
submission before these catalog requests show the new cartridge.

Download the published physical artifact for a final smoke test:

```bash
curl "$PRG32_STORE_URL/api/games/org.uniparthenope.hello-world/download?architecture=esp32c6&version=1.0.0" \
  --output build-esp32c6/hello_world_from_store.prg32

python3 tools/prg32_game.py upload \
  build-esp32c6/hello_world_from_store.prg32 \
  --url http://192.168.4.1
```

## 16. Common Missing Tool Fixes

| Symptom | Likely cause | Fix |
|---|---|---|
| `idf.py: command not found` | ESP-IDF shell not exported | Run `. $HOME/esp-idf/export.sh` or use ESP-IDF PowerShell |
| `missing tool: riscv32-esp-elf-gcc` | ESP-IDF toolchain missing or not on `PATH` | Run `./install.sh esp32c3,esp32c6`, then export ESP-IDF |
| `ninja: command not found` | Host build tool missing | Install Ninja with the platform package manager |
| QEMU build cannot find virtual RGB component | Wrong target or defaults | Use `esp32c3` and `sdkconfig.defaults.qemu` |
| Physical display is black | QEMU build flashed to board or wrong pins | Rebuild `build-esp32c6` with `sdkconfig.defaults`; check `main/prg32_config.h` |
| Upload cannot reach board | Host is not on PRG32 Wi-Fi or wrong URL | Connect to `PRG32` AP and use `http://192.168.4.1` |
| Store publish returns `401` | Missing or invalid token | Ask for the classroom token or omit auth only on open stores |
| Store publish returns `400` | Bad manifest or zip layout | Check `manifest.json` and `unzip -l` output |

## 17. What To Read Next

- `docs/cartridges.md`: deeper cartridge workflow and slot behavior.
- `docs/qemu.md`: host-specific QEMU setup and troubleshooting.
- `docs/tutorial.md`: first assembly game tutorial.
- `docs/tutorial_graphic_game.md`: graphics assembly tutorial.
- `docs/tutorial_c_game.md`: C cartridge tutorial.
- `docs/api.md`: board HTTP API and Cartridge Store API reference.
- `docs/framework_manual.md`: PRG32 runtime API and ABI details.
- `docs/assets.md`: image, sprite, tile, GIF, and audio asset conversion.
