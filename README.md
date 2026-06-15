# PRG32

PRG32 is an educational runtime for RISC-V assembly and C games.

## Academic Profile

- Project domain: Embedded Systems and Computer Architecture Education
- Platform focus: ESP32-C6 (hardware) and ESP32-C3 QEMU path (desktop emulation)
- Course style: first-year/early undergraduate assembly and systems labs
- Academic supervisor / project lead: Raffaele Montella - UniParthenope
- Contributor (student): Ivan Cafiero - UniParthenope - Computer Science student
- Contributor (student): Simone Boscaglia - UniParthenope - Computer Science student

## 🚀 Quick Start (macOS)

```bash
# 1) Dependencies
brew install git cmake ninja dfu-util ccache libusb python

# 2) ESP-IDF
cd $HOME
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3,esp32c6
. ./export.sh

# 3) Project
cd <path_to_PRG32>

# 4) Build and flash physical ESP32-C6 firmware
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor

# 5) Build QEMU firmware
./scripts/qemu/build_qemu.sh
```

Open a second terminal (source ESP-IDF again), then stage a demo cartridge:

```bash
cd <path_to_PRG32>
. $HOME/esp-idf/export.sh
python3 -m prg32 build \
  examples/games/asteroids/graphics/game.S \
  --portable \
  --entry-prefix asteroids_graphics \
  --name asteroids \
  --out build-qemu/asteroids.prg32
python3 -m prg32 upload-qemu build-qemu/asteroids.prg32 --flash build-qemu/qemu_flash.bin
```

Run everything with one command next time:

```bash
./scripts/run_qemu_demo.sh
```

## Documentation Index

Start with
[docs/getting_started_game_development.md](docs/getting_started_game_development.md)
when setting up a new Windows, Linux, or macOS development machine. It covers
ESP-IDF, PlatformIO, QEMU, physical upload, a from-scratch hello world
cartridge, and a manual Cartridge Store publishing bundle.

Core workflow documents:

- [docs/deployment.md](docs/deployment.md): build, flash, monitor, setup mode,
  QEMU, and the resident firmware deployment model.
- [docs/qemu.md](docs/qemu.md): desktop virtual-screen setup, ESP32-C3 QEMU
  target notes, cartridge staging, and QEMU troubleshooting.
- [docs/cartridges.md](docs/cartridges.md): `.prg32` cartridge build/upload
  workflow, runtime ABI lookup, hardware slots, QEMU staging, and AUDIO blocks.
- [docs/api.md](docs/api.md): board HTTP endpoints, score API, runtime
  metadata, screenshots, Cartridge Store API, and MetricsServer API.
- [docs/cartridge-format.md](docs/cartridge-format.md): binary `.prg32`
  package layout and optional AUDIO block format.
- [docs/framework_manual.md](docs/framework_manual.md): PRG32 C/assembly ABI,
  graphics, input, audio, setup mode, Wi-Fi, metrics, and cartridge loader APIs.
- [docs/abi.md](docs/abi.md): compact ABI reference for cartridge-facing
  functions, constants, input bits, status bands, and setup helpers.
- [docs/hardware.md](docs/hardware.md): board, display, input, audio, and
  external controller architecture.
- [docs/external_controllers.md](docs/external_controllers.md): UART packet
  bridge for external controllers and two-player input.
- [docs/ili9341_notes.md](docs/ili9341_notes.md): hardware display notes and
  RGB565 byte-order reminders.

Learning and classroom documents:

- [docs/tutorial.md](docs/tutorial.md): first assembly game tutorial and
  cartridge packaging introduction.
- [docs/tutorial_ascii_game.md](docs/tutorial_ascii_game.md): text-mode
  assembly game tutorial.
- [docs/tutorial_graphic_game.md](docs/tutorial_graphic_game.md): graphics
  assembly tutorial with draw calls and state updates.
- [docs/tutorial_c_game.md](docs/tutorial_c_game.md): C game tutorial using the
  same runtime ABI and cartridge workflow.
- [docs/examples.md](docs/examples.md): example game and feature-demo overview.
- [examples/games/README.md](examples/games/README.md): run and package the
  standard game examples.
- [docs/teaching_with_prg32.md](docs/teaching_with_prg32.md): instructor notes,
  course sequencing, and assembly/C comparison tracks.
- [docs/labs/README.md](docs/labs/README.md): lab sequence overview.
- [docs/labs/lab_01_hello_world.md](docs/labs/lab_01_hello_world.md): first
  output and build checkpoint.
- [docs/labs/lab_02_input.md](docs/labs/lab_02_input.md): joystick bitmask
  input lab.
- [docs/labs/lab_03_graphics.md](docs/labs/lab_03_graphics.md): drawing and
  viewport lab.
- [docs/labs/lab_04_sound.md](docs/labs/lab_04_sound.md): audio and sample
  playback lab.
- [docs/labs/lab_05_scores_and_controllers.md](docs/labs/lab_05_scores_and_controllers.md):
  score API, runtime query, and external controller lab.
- [docs/labs/debugging_memory_inspection.md](docs/labs/debugging_memory_inspection.md):
  inspect memory state while debugging student games.
- [docs/labs/debugging_register_tracing.md](docs/labs/debugging_register_tracing.md):
  trace RISC-V register state across calls.
- [docs/labs/break_fix_assignments.md](docs/labs/break_fix_assignments.md):
  assessment-friendly broken examples and repair prompts.

Assets, audio, and measurement documents:

- [docs/assets.md](docs/assets.md): image, GIF, sprite, tile, and asset
  conversion tools.
- [docs/audio.md](docs/audio.md): I2S audio wiring, audio APIs, samples,
  tracker-style assets, and cartridge AUDIO blocks.
- [docs/score_api.md](docs/score_api.md): board-local and optional Flask/SQLite
  score service endpoints.
- [docs/metrics_api.md](docs/metrics_api.md): setup-mode performance test,
  streaming metrics, report export, and server endpoints.
- [docs/scientific_measurement_tutorial.md](docs/scientific_measurement_tutorial.md):
  reproducible performance measurement workflow for papers and lab reports.
- [docs/images/README.md](docs/images/README.md): screenshot and documentation
  image capture guidance.

Recommended reading paths:

- Build the firmware: read `docs/getting_started_game_development.md`, then
  `docs/deployment.md`; use `docs/qemu.md` for the virtual screen.
- Create a new game: start with `docs/tutorial.md` for assembly or
  `docs/tutorial_c_game.md` for C, then read `docs/cartridges.md`.
- Upload a cartridge to hardware: read `docs/getting_started_game_development.md`
  sections 11-13, then `docs/cartridges.md`.
- Publish a game package: read `docs/getting_started_game_development.md`
  sections 14-15, then the Cartridge Store section of `docs/api.md`.
- Convert images or audio: read `docs/assets.md` and `docs/audio.md`.
- Run performance tests: read `docs/metrics_api.md`, then
  `docs/scientific_measurement_tutorial.md`.
- Contribute game examples: read `docs/tutorial_ascii_game.md`,
  `docs/tutorial_graphic_game.md`, `docs/tutorial_c_game.md`, and
  `examples/games/README.md`.
- Contribute firmware changes: read `docs/framework_manual.md`,
  `docs/abi.md`, `docs/deployment.md`, and the validation checklist in
  `AGENTS.md`.
- Prepare or teach a lab: read `docs/teaching_with_prg32.md` and
  `docs/labs/README.md`.

## 🍏 macOS Setup (Tested on Apple Silicon)

```bash
brew install git cmake ninja dfu-util ccache libusb python
cd $HOME
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3,esp32c6
. ./export.sh
```

Quality-of-life alias:

```bash
alias get_idf=". $HOME/esp-idf/export.sh"
```

Common pitfalls:

- New shell opened: run `get_idf` again.
- `idf.py` missing: ESP-IDF not sourced.
- `riscv32-esp-elf-gcc` missing: ESP-IDF toolchain not installed/sourced.

## Windows Setup

Recommended classroom path:

1. Install Git for Windows.
2. Install Visual Studio Code.
3. Install the Espressif ESP-IDF extension for VS Code.
4. Install the PlatformIO extension for VS Code.
5. Install the Microsoft C/C++ and Python extensions.

Standalone ESP-IDF path:

1. Download and run the Espressif ESP-IDF Tools Installer for Windows.
2. Select an ESP-IDF 5.3 or newer release.
3. Include support for `esp32c3` and `esp32c6`.
4. Open the "ESP-IDF PowerShell" shortcut created by the installer.
5. Clone and build PRG32 from that shell:

```powershell
cd $HOME\Documents
git clone https://github.com/raffmont/PRG32.git
cd PRG32
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6\sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6\sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6\sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor
```

PlatformIO path:

```powershell
cd $HOME\Documents\PRG32
pio run
pio run -t upload
pio device monitor -b 115200
```

QEMU path:

```powershell
idf.py -B build-qemu -D SDKCONFIG=build-qemu\sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3
idf.py -B build-qemu -D SDKCONFIG=build-qemu\sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu build
idf.py -B build-qemu -D SDKCONFIG=build-qemu\sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

Windows notes:

- Use the ESP-IDF PowerShell or ESP-IDF Command Prompt, not a plain terminal
  where `idf.py` has not been exported.
- If flashing fails, check Device Manager for the ESP32-C6 serial port and pass
  it explicitly with `-p COMx`.
- If PlatformIO Monitor cannot open the port, close Arduino Serial Monitor,
  ESP-IDF Monitor, and every other terminal using the same COM port.
- Use the checked-in `PRG32.code-workspace` for student labs; paths are
  workspace-relative.

## Linux Setup

The commands below target Debian/Ubuntu. On Fedora, Arch, and other
distributions, install the equivalent packages.

```bash
sudo apt update
sudo apt install -y \
  git wget flex bison gperf python3 python3-venv python3-pip \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
  libusb-1.0-0
```

Install ESP-IDF:

```bash
cd $HOME
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3,esp32c6
. ./export.sh
```

Build and flash PRG32:

```bash
cd $HOME
git clone https://github.com/raffmont/PRG32.git
cd PRG32
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor
```

Linux serial permissions:

```bash
sudo usermod -aG dialout "$USER"
```

Log out and back in after changing group membership. ESP32-C6 boards usually
appear as `/dev/ttyACM0`; USB serial adapters may appear as `/dev/ttyUSB0`.

PlatformIO CLI path:

```bash
python3 -m venv .venv-platformio
. .venv-platformio/bin/activate
python3 -m pip install platformio
pio run
pio run -t upload
pio device monitor -b 115200
```

QEMU path:

```bash
./scripts/qemu/build_qemu.sh
```

Linux notes:

- Run `. $HOME/esp-idf/export.sh` in every new shell before using `idf.py`.
- If the board is visible but flashing fails, check `dialout` membership and
  reconnect the USB cable after logging in again.
- Keep build directories separate: `build-esp32c6` for hardware and
  `build-qemu` for desktop graphics testing.
- For reproducible scientific measurements, use
  `sdkconfig.defaults;sdkconfig.defaults.metrics` as described in
  `docs/scientific_measurement_tutorial.md`.

## PlatformIO Quick Start

Open the repository root in PlatformIO. The checked-in `platformio.ini` default
environment is `prg32-esp32c6`, which targets the ESP32-C6 DevKitC-1 with
ESP-IDF, reuses the standard `main` component and applies
`partitions_prg32.csv` plus `sdkconfig.defaults`.

CLI equivalents:

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

The ESP32-C6 build keeps UART0 as the primary ESP-IDF console and enables
native USB Serial/JTAG as a secondary output for PlatformIO Monitor. A healthy
boot logs the configured `prg32_lcd` ILI9341 pins before drawing the splash.

The PlatformIO environment is for the physical ESP32-C6 classroom board. Keep
using the `idf.py` commands in `docs/qemu.md` for QEMU screen builds.

## 🧠 How PRG32 works

- PRG32 is **not** a CPU instruction emulator.
- Code runs natively on ESP32-C6 hardware, or on Espressif QEMU firmware target
  ESP32-C3 for desktop graphics/testing.
- Games are distributed as cartridges (`.prg32`) loaded by the runtime.
- Store-ready cartridges may append a backward-compatible `PRG32META` trailer
  with metadata, icon, optional screenshot/signature, and a standard colophon.
- Input uses one local digital joystick through the same PRG32 bitmask used by
  QEMU and cartridge tests.
- Hold A + B + DOWN on the local joystick to restart the PRG32 firmware.
- Optional cartridge multiplayer shares player snapshots over Wi-Fi station
  mode and WebSocket through a small Node.js server.
- Audio supports mandatory mono I2S output and optional stereo PRG32 Audio Plus
  using MAX98357A amplifier breakouts.
- Optional performance metrics can upload buffered frame timing samples to the
  standalone
  [MetricsServer](https://github.com/riscv-prg32/MetricsServer) for labs and
  regression checks.

Flow:

```text
.S source -> riscv toolchain -> .prg32 cartridge -> PRG32 runtime -> init/update/draw loop
```

## Portable Cartridges

PRG32 cartridges are portable across firmware builds that implement the same
cartridge ABI. Portable cartridges call firmware services through a versioned
ABI table instead of absolute firmware symbol addresses.

Build a portable cartridge:

```bash
python3 -m prg32 build examples/games/pong/ascii/game.S \
  --entry-prefix pong_ascii \
  --portable \
  --out build/pong.prg32
```

Inspect it:

```bash
python3 -m prg32 summary build/pong.prg32
```

The summary shows ABI major/minor, ABI hash, import model, and required or
optional feature bits. Legacy absolute-import cartridges are still supported for
old workflows, but they are tied to the firmware image used at build time and
are not guaranteed to run on another firmware. ABI hash mismatches, missing
required features, and incompatible legacy cartridges are rejected by the
runtime, store download path, QEMU staging path, and HTTP upload tool with a
diagnostic message.

Build all checked-in examples as portable cartridges and CartridgeStore bundles:

```bash
python3 tools/prg32_build_portable_examples.py --clean
```

Prepare or flash a published single-file legacy firmware image:

```bash
python3 -m prg32 esp32c6 prepare-legacy
python3 -m prg32 esp32c6 flash-legacy \
  publish/legacy-firmware/PRG32-legacy-esp32c6.json \
  --port /dev/cu.usbmodem5ABA0099241
```

See [docs/publishing_and_flashing_legacy_firmware.md](docs/publishing_and_flashing_legacy_firmware.md).

Cartridge metadata and store publishing are documented in
[docs/cartridge_metadata.md](docs/cartridge_metadata.md),
[docs/colophon_abi.md](docs/colophon_abi.md),
[docs/cartridge_store.md](docs/cartridge_store.md), and
[docs/setup_mode_cartridge_store.md](docs/setup_mode_cartridge_store.md). The
download server is the standalone **Cartridge Store** in
`riscv-prg32/CartridgeStore`; it is not part of this firmware repository.

## ❗ Troubleshooting

- `idf.py: command not found`: ESP-IDF is not sourced. Run
  `. $HOME/esp-idf/export.sh`.
- Black display on real ESP32-C6 hardware: rebuild with the physical build
  directory, not the QEMU one:
  `idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6`
  then
  `idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor`.
  The monitor should log `prg32_lcd` with the configured ILI9341 pins.
- PlatformIO Monitor shows only ROM boot text or only its header: the firmware
  was probably built with an older generated `sdkconfig.prg32-esp32c6`, or the
  USB monitor missed secondary-console output during reset. Delete that
  generated file once, rebuild, upload, open Monitor again, and press RESET/EN
  on the board. A healthy app boot logs `PRG32 boot: app_main entered` and the
  configured `prg32_lcd` pins.
- PlatformIO says it cannot exclusively lock `/dev/cu.usbmodem...`: close every
  other Serial Monitor, ESP-IDF Monitor, Arduino Serial Monitor, and terminal
  using that port, then start only one PlatformIO Monitor.
- QEMU runs but the game does not move: focus the QEMU monitor terminal and use
  arrow keys or `W`/`A`/`S`/`D` for joystick 1, `Enter`/`Space` for SELECT,
  `J`/`Z` for A, and `K`/`X` for B.
- QEMU runs but the game does not move: focus the terminal running QEMU. Use
  arrow keys or `W`/`A`/`S`/`D` for joystick 1, `Enter`/`Space` for SELECT,
  `J`/`Z` for A, and `K`/`X` for B.
- Cartridge upload fails: `build-qemu/qemu_flash.bin` is missing/invalid, or the
  cartridge is too large. Run QEMU once, then rerun `upload-qemu`.
- `riscv32-esp-elf-gcc` missing: re-run `./install.sh esp32c3,esp32c6` and
  source the ESP-IDF export script.
- Partition mismatch errors: run `python3 -m prg32 doctor` and verify
  `partitions_prg32.csv` plus the selected cartridge slot.

## Learning Path

1. Build and flash the resident firmware.
2. Open setup with A+B at boot, configure Wi-Fi or CartridgeStore, and upload a cartridge.
3. Read `docs/tutorial.md` for assembly or `docs/tutorial_c_game.md` for C.
4. Complete the labs in `docs/labs`.
5. Modify one example game under `examples/games`.
6. Try a focused rendering demo under `examples/features`.
7. Package it as a `.prg32` cartridge and upload it over Wi-Fi or stage it into
   the QEMU flash image.
8. For research-style measurements, follow
   [docs/scientific_measurement_tutorial.md](docs/scientific_measurement_tutorial.md).

The intended student rhythm is small and visible: change one thing, run it,
observe the result, and write down what changed.

## Feature Demos

The `examples/features` directory contains focused demos for framework features:

- scrolling and parallax playfields
- animated sprites
- dual playfield rendering
- full-screen firmware splash and 320x200 game title splash screens
- upper/lower status bands for FPS, Wi-Fi, game info, and debug text
- joystick-driven on-screen keyboard input with printable ASCII support
- Wi-Fi setup mode
- cartridge multiplayer API with the external Node.js relay server
- CartridgeStore integration for catalog discovery and cartridge downloads
- setup audio menu with output detection, volume, and test tune
- configurable onboard RGB LED API and optional audio VU meter
- audio synthesis and sample playback

Use these before building a full game when the lesson is about one graphics
technique rather than game rules.

## Audio

PRG32 supports retro-style digital audio through I2S.

The default configuration uses one MAX98357A I2S DAC/amplifier breakout for
mono output. Optional stereo output, called PRG32 Audio Plus, uses two
MAX98357A breakouts on the same I2S bus: one configured for the left channel
and one configured for the right channel.

Start with:

- `examples/features/audio_mono_beep`
- `examples/features/audio_mono_sample`
- `examples/features/audio_mono_tracker`
- `examples/features/audio_stereo_pan_test`
- `examples/features/audio_stereo_music`

See [docs/audio.md](docs/audio.md) for wiring, API, cartridge AUDIO blocks, and
troubleshooting.

The resident firmware also shows a full 320x240 logo splash screen on boot.
Physical ESP32-C6 builds enter setup when A+B are held during boot, when no
cartridge is stored, or when multiple cartridges exist without a default. The
setup menu can run a cartridge, set the default boot cartridge, configure Wi-Fi,
configure CartridgeStore access, browse the store, open the audio setup menu,
open the developer status-band menu, launch the unattended performance test,
or show the about screen.
Setup screens show the active Wi-Fi mode and current IP address,
and the local joystick can navigate them with SELECT/B to confirm and A to go
back. The former setup device demo now lives as the
[DeviceDemo cartridge](https://github.com/riscv-prg32/DeviceDemo), which can be
built, uploaded, and published through CartridgeStore like the teaching games.
When the audio configuration is usable for the current board,
the splash plays a short welcome sound; otherwise it falls back to the passive
buzzer when configured.

## Example Games

The `examples/games` directory includes ASCII assembly, graphics assembly, and
C versions of:

- `pong`
- `breakout`
- `space_invaders`
- `pacman`
- `asteroids`
- `tetris`
- `platformer`
- `raycaster`
- `wing_commander`
- `frogger`

See [examples/games/README.md](examples/games/README.md) for step-by-step
instructions to run each game embedded in firmware or as an uploadable
cartridge on hardware and QEMU.

## Teaching Tracks

PRG32 supports two classroom tracks over the same runtime:

- Computing Architecture: RISC-V assembly examples under `examples/games/*/ascii`
  and `examples/games/*/graphics`.
- C Programming: C examples under `examples/games/*/c` and
  `examples/features/*/c`.

See [docs/teaching_with_prg32.md](docs/teaching_with_prg32.md) for trainer notes,
lab sequencing, and how to compare assembly and C versions of the same example.
The C programming tutorial is [docs/tutorial_c_game.md](docs/tutorial_c_game.md).

## Runtime APIs

- Runtime/diagnostics: `GET /api/runtime`
- Games: `GET /api/games`, `POST /api/games?slot=cart0`, `POST /api/games/select?slot=cart0`
- Screenshot: `GET /api/screenshot.bmp`
- Board-local scores: `GET /api/scores`, `POST /api/scores`
- Performance test: `GET /api/performance.json`
- Classroom score server:
  [ScoreServer](https://github.com/riscv-prg32/ScoreServer) provides the same
  score API with SQLite persistence.
- Optional metrics server:
  [MetricsServer](https://github.com/riscv-prg32/MetricsServer) provides
  `POST /api/runs`, `POST /api/metrics/batch`, and
  `GET /api/runs/<run_id>/report.md`
- Multiplayer relay: run
  [MultiplayerServer](https://github.com/riscv-prg32/MultiplayerServer) on a
  classroom host and set `PRG32_MULTIPLAYER_SERVER_URL`.

`/api/runtime` includes firmware version, cartridge state, frame count, and last input state.
`/api/screenshot.bmp` returns the current 320x240 framebuffer as a BMP image.
`/api/performance.json` returns the latest setup-mode unattended benchmark run,
including raw samples and aggregate windows.

See [docs/metrics_api.md](docs/metrics_api.md) for the opt-in firmware metrics
configuration, setup-mode performance test, `tools/prg32_metrics_paper.py`, and
the standalone
[MetricsServer](https://github.com/riscv-prg32/MetricsServer) reporting
workflow. See
[docs/scientific_measurement_tutorial.md](docs/scientific_measurement_tutorial.md)
for a step-by-step scientific-paper measurement workflow with screenshots.

## Asset Tools

- `tools/prg32_image_convert.py`: convert PNG/JPEG/GIF assets to C or assembly.
- `tools/prg32_image_prepare.py`: interactive terminal preparation helper.
- `tools/prg32_audio_convert.py`: convert WAV samples or MIDI tracks.
- `tools/wav2prg32sample.py`: convert WAV to unsigned 8-bit PRG32 sample data.
- `tools/midi2prg32audio.py`: convert simple MIDI notes to tracker JSON.
- `tools/prg32audio_pack.py`: pack samples, instruments, and tracks into an
  AUDIO block for `.prg32` cartridges.
- `python3 -m prg32 attach-metadata`: append a deterministic `PRG32META`
  trailer for Cartridge Store publishing.
- `python3 -m prg32 inspect-metadata`: inspect metadata, assets, signature,
  colophon, and unknown trailer blocks.
- `python3 -m prg32 store-discover`: find CartridgeStore instances via mDNS.
- `python3 -m prg32 store-list`: print a CartridgeStore catalog table.
- `python3 -m prg32 store-download`: download a `.prg32` from a store.
- `python3 -m prg32 publish`: build a cartridge and submit a store bundle.
- `python3 -m prg32 pack-bundle`: create a flat multi-architecture zip.
- `python3 -m prg32 publish-bundle`: submit a prepared bundle.
- `tools/prg32_build_portable_examples.py`: build every checked-in example as
  portable `.prg32` cartridges and CartridgeStore bundles.
- `python3 -m prg32 esp32c6 prepare-legacy`: merge a physical firmware build into
  one publishable binary.
- `python3 -m prg32 esp32c6 flash-legacy`: flash a published single-file legacy
  firmware image.

See [docs/assets.md](docs/assets.md).

## Developer Tools

- Doctor check:

```bash
python3 -m prg32 doctor
```

- One-shot QEMU demo:

```bash
./scripts/run_qemu_demo.sh
```

## 🧪 Smoke Test

This script verifies the basic PRG32 workflow end to end: environment setup,
firmware build, cartridge build, and QEMU staging.

```bash
./scripts/smoke_test.sh
```

Expected result:

- all steps print `[OK]`
- warnings may appear but are non-blocking
- final output shows `=== SMOKE TEST PASSED ===`

For GitHub Actions and machines without ESP-IDF, the repository also provides a
host-only smoke test:

```bash
bash scripts/ci_smoke_test.sh
```

That check runs Python syntax checks, unit tests, whitespace validation, and the
cartridge tool doctor in host-only mode.

## Screenshots

Place images under `docs/images/`.

Suggested files:

- `docs/images/prg32-qemu-game.png`
- `docs/images/prg32-upload-success.png`
- `docs/images/prg32-runtime-api.png`

See `docs/images/README.md` for capture instructions.

## Repo Map

- `components/prg32`: runtime implementation
- `main`: default firmware app
- `examples/games`: assembly and C game demos
- `examples/features`: focused firmware feature demos
- `python3 -m prg32`: cartridge tooling
- `tools/prg32_image_convert.py`: image and animation converter
- `tools/prg32_audio_convert.py`: sample and MIDI converter
- `tools/prg32audio_pack.py`: AUDIO block packer
- Classroom score server:
  [riscv-prg32/ScoreServer](https://github.com/riscv-prg32/ScoreServer)
- Multiplayer relay:
  [riscv-prg32/MultiplayerServer](https://github.com/riscv-prg32/MultiplayerServer)
- `docs`: tutorials, labs, API docs
- `.github/workflows/ci.yml`: GitHub Actions smoke and firmware build workflow
- `tests`: host-side unit tests for tooling and documentation hygiene

## Repository Structure (Academic View)

- `components/prg32`: framework API/ABI and hardware abstraction layer
- `main`: reference runtime firmware app (minimal and teachable baseline)
- `examples/games`: course demos in RISC-V assembly and C
- `examples/features`: focused rendering demos in RISC-V assembly and C
- `docs/labs`: lab handouts, debugging assignments, and assessment-friendly exercises
- `tools`: reproducible developer tooling (cartridge builder, QEMU scripts,
  setup performance report tooling)
- `hardware`: architecture notes and board integration scaffold

## Contributors

- Raffaele Montella - UniParthenope - academic supervisor / project lead
- Ivan Cafiero - UniParthenope - Computer Science student
- Simone Boscaglia - UniParthenope - Computer Science student


See [CONTRIBUTORS.md](CONTRIBUTORS.md) for contributor metadata suitable for
academic submissions.

## Citation

For reports, theses, or coursework submissions, use the citation metadata in
[CITATION.cff](CITATION.cff).
