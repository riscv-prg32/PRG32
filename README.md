# PRG32
<p align="center">
  <img src="assets/prg32_logo.png" alt="PRG32 logo" width="300">
</p>
PRG32 is an open-source educational runtime for teaching RISC-V assembly and C programming in undergraduate computer architecture and systems courses. PRG32 exposes a minimal interface built around the init/update/draw game loop.

PRG32 provides an entire retro-gaming environment to run native RV32IMAC machine code on the Espressif ESP32-C6, a commercially available microcontroller. The platform includes embedded firmware, a .prg32 cartridge format and toolchain, a QEMU-based desktop emulator and can be used in combination with the Cartridge Store, a server providing app-store-style distribution, multiplayer relay, score tracking, and frame-level metrics collection.

PRG32 is **not** a CPU instruction emulator. Code runs natively on ESP32-C6 hardware, or on Espressif QEMU firmware target ESP32-C3 for desktop graphics/testing.

## Academic Profile

- Project domain: Embedded Systems and Computer Architecture Education
- Platform focus: ESP32-C6 (hardware) and ESP32-C3 QEMU path (desktop emulation)
- Course style: first-year/early undergraduate assembly and systems labs
- Academic supervisor / project lead: Raffaele Montella - UniParthenope
- Contributor (student): Simone Boscaglia - UniParthenope - Computer Science student
- Contributor (student): Ivan Cafiero - UniParthenope - Computer Science student

## Install & Setup

<details>
<summary><strong>Windows</strong></summary>

1. Install Git for Windows and Python 3.
2. Download and run the Espressif ESP-IDF Tools Installer for Windows.
3. Select an ESP-IDF 5.4 or newer release.
4. Include support for `esp32c3` and `esp32c6`.
5. (Optional) Check the QEMU RISC-V emulator box during installation if you want to use the QEMU desktop emulator.
6. Open the "ESP-IDF PowerShell" shortcut created by the installer.
7. Clone the project and verify your setup from that shell:

```powershell
cd $HOME\Documents
git clone https://github.com/riscv-prg32/PRG32
cd PRG32
python -m prg32 doctor
```

#### Notes
- Use the ESP-IDF PowerShell or ESP-IDF Command Prompt, not a plain terminal
where `idf.py` has not been exported.
- If flashing fails, check Device Manager for the ESP32-C6 serial port and pass
  it explicitly with `-p COMx`.
- If PlatformIO Monitor cannot open the port, close Arduino Serial Monitor,
  ESP-IDF Monitor, and every other terminal using the same COM port.
</details>

<details>
<summary><strong>Linux</strong></summary>

Install dependencies (Debian/Ubuntu):
```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-venv python3-pip cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 curl zip
```

Install QEMU native libraries (optional, required for QEMU desktop emulator only):
```bash
sudo apt install -y libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0
```

Install ESP-IDF:
```bash
cd $HOME
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3,esp32c6
. ./export.sh
# Optional: install QEMU desktop emulator
python $IDF_PATH/tools/idf_tools.py install qemu-riscv32
```

Clone the project and verify your setup:
```bash
git clone https://github.com/riscv-prg32/PRG32
cd PRG32
python3 -m prg32 doctor
```

Linux serial permissions:
```bash
sudo usermod -aG dialout "$USER"
```
Log out and back in after changing group membership.

#### Notes
ESP32-C6 boards usually appear as `/dev/ttyACM0`; USB serial adapters may appear as `/dev/ttyUSB0`. If the board is visible but flashing fails, check `dialout` membership and
reconnect the USB cable after logging in again.

</details>

<details>
<summary><strong>macOS</strong></summary>

Install dependencies:
```bash
brew install git cmake ninja dfu-util ccache libusb python curl zip
```

Install QEMU native libraries (optional, required for QEMU desktop emulator only):
```bash
brew install libgcrypt glib pixman sdl2 libslirp
```

Install ESP-IDF:
```bash
cd $HOME
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3,esp32c6
. ./export.sh
# Optional: install QEMU desktop emulator
python $IDF_PATH/tools/idf_tools.py install qemu-riscv32
```

Clone the project and verify your setup:
```bash
git clone https://github.com/riscv-prg32/PRG32
cd PRG32
python3 -m prg32 doctor
```
</details>

<details>
<summary><strong>PlatformIO (Alternative)</strong></summary>

While ESP-IDF standalone is the primary and recommended way to install and build PRG32, you can also use PlatformIO. Open the repository root in PlatformIO. The checked-in `platformio.ini` default environment targets the ESP32-C6.

**CLI Setup (Linux/macOS):**
```bash
python3 -m venv .venv-platformio
. .venv-platformio/bin/activate
python3 -m pip install platformio
pio run
pio run -t upload
pio device monitor -b 115200
```

**VS Code Setup (Windows):**
1. Install Git for Windows.
2. Install Visual Studio Code.
3. Install the Espressif ESP-IDF extension for VS Code.
4. Install the PlatformIO extension for VS Code.
5. Install the Microsoft C/C++ and Python extensions.

- Use the checked-in `PRG32.code-workspace` for student labs; paths are
  workspace-relative.
  
**CLI Setup (Windows):**
```powershell
cd $HOME\Documents\PRG32
pio run
pio run -t upload
pio device monitor -b 115200
```

The ESP32-C6 build keeps UART0 as the primary ESP-IDF console and enables
native USB Serial/JTAG as a secondary output for PlatformIO Monitor. A healthy
boot logs the configured `prg32_lcd` ILI9341 pins before drawing the splash.

The PlatformIO environment is for the physical ESP32-C6 classroom board. Keep
using the `idf.py` commands in `docs/qemu.md` for QEMU screen builds.
</details>

> [!TIP]
> After installing PRG32, read [Getting Started With PRG32](docs/usage/getting_started.md) to learn how to run your first cartridge.

> [!IMPORTANT]
> For assistance with setup or execution issues, please refer to the [troubleshooting guide](docs/usage/troubleshooting.md).


## Documentation Index

**Guides & Workflows:**
- [Getting Started With PRG32](docs/usage/getting_started.md): End-to-end setup and manual.
- [QEMU Virtual Screen](docs/usage/qemu.md): Desktop testing and troubleshooting.
- [Cartridges](docs/software/cartridges.md): The `.prg32` build/upload workflow.
- [Cartridge CI/CD](docs/software/cartridges.md#continuous-integration-and-delivery-artifacts): Automated Blackjack and DeviceDemo packages.
- [Hardware & Pinouts](docs/hardware/hardware.md): Board, display, and input architecture.

**Learning Materials:**
- [Teaching with PRG32](docs/learn/teaching_with_prg32.md): Instructor notes and classroom setup.
- [Assembly Tutorial](docs/learn/tutorial.md) | [C Tutorial](docs/learn/tutorial_c_game.md)
- [Labs Overview](docs/learn/labs/README.md)
- [Example Games](docs/learn/examples.md)

**APIs & Advanced Features:**
- [Framework C/Assembly ABI](docs/software/framework_manual.md)
- [HTTP APIs (Score, Metrics, Multiplayer)](docs/software/api.md)
- [Audio Guide](docs/tools/audio.md)
- [Assets Tools](docs/tools/assets.md)

**Additional References:**
- [Repository Structure](docs/repository_structure.md)
- [Publishing Firmware](docs/usage/publishing_and_flashing_firmware.md)

## Contributors

- Raffaele Montella - UniParthenope - academic supervisor / project lead
- Simone Boscaglia - UniParthenope - Computer Science student
- Ivan Cafiero - UniParthenope - Computer Science student

See [CONTRIBUTORS.md](CONTRIBUTORS.md) for contributor metadata suitable for academic submissions.

## Citation

For reports, theses, or coursework submissions, use the citation metadata in [CITATION.cff](CITATION.cff).
