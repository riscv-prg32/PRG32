# PRG32 Unified Tooling

The PRG32 project ships with a unified Python package, `prg32`.

You run the tooling as a module via Python:
```bash
python3 -m prg32 <command> [subcommand] [options]
```

> [!TIP]
> The PRG32 CLI supports command and subcommand abbreviations! You can type unique prefixes instead of full names. For example, `python3 -m prg32 e b` is equivalent to `python3 -m prg32 esp32c6 build`.
## Main Commands
- `esp32c6`: ESP32C6 SoC tasks
- `qemu`: QEMU emulator tasks
- `cartridge`: Cartridge tasks (build, summary, etc.)
- `python`: PRG32 Python tasks (new, check, to-c, to-asm, build)
- `abi`: ABI tooling tasks
- `store`: CartridgeStore and metadata tasks
- `doctor`: Check local toolchain prerequisites
- `runtime`: Print runtime linker information
- `debug`: Control an optional PRG32-QT debugger over its device HTTP API
- `pins`: List available pin setup possibilities

## ESP32C6 SoC Tasks (`esp32c6`)
All commands interacting with physical ESP32-C6 hardware fall under the `esp32c6` group.

### Firmware Operations
- `build`: build the ESP32C6 firmware. Cleans `./sdkconfig` and `build-esp32c6/sdkconfig` before configuring to guarantee local default configurations (`profiles/sdkconfig.defaults.local`) are always applied. Use `-k` / `--keep-sdkconfig` (or `--no-erase-sdkconfig`) to disable erasing `sdkconfig` files and preserve existing manual menuconfig changes. Use `-p <name>` / `--pin-profile <name>` (or `--pins <name>`) to select the hardware pin configuration profile (e.g. `prototype1`, see [PRG32 Profiles](profiles.md#hardware-pin-profiles)). You can see all available pin setup possibilities with `python3 -m prg32 esp32c6 pins`. If no pin profile is supplied and no Custom Profile is configured in `sdkconfig`, the build command will fail fast. Use `-s` / `--skip-target` to decrease compilation time if the ESP32C6 target was already set (e.g. from a previous build). Use `-d` / `--debug` for debug profiles and `-e` / `--enable-heap-tracking` for heap task tracking.
- `flash`: flash the ESP32C6 image. Does not do anything when a cartridge is running!
- `pins`: list available pin setup possibilities discovered in `profiles/` (e.g. `prototype1`).
- `erase-flash`: erase flash of ESP32C6. It is sometimes useful to completely erase the flash for debugging purposes.
    - `reset`: erase flash and re-flash. Combination of `erase-flash` and `flash`, for convenience. Erases `./sdkconfig` and `build-esp32c6/sdkconfig` (disable with `-k` / `--keep-sdkconfig` / `--no-erase-sdkconfig`).
- `memory`: get static and dynamic memory analysis of the ESP32C6 SoC (See [docs/memory.md](/docs/hardware/memory.md)
### Upload & Run Cartridges
- `upload`: upload over HTTP with `--url`, or use the automatic running-device
  protocol with `--port`. The latter supports native USB and
  `socket://HOST:PORT` emulator endpoints, requires no reset or upload mode,
  and accepts `--command store|run|debug|performance`. See the
  [device protocol](../software/usb_protocol.md). Browsers use the same
  protocol; see [Web-to-USB cartridge deployment](../software/web_usb_deployment.md).
- `run`: run a stored slot over HTTP or the device protocol. `--debug` requests
  instruction debugging on PRG32-Qt; physical firmware reports it unsupported.

### Firmware Tools
These scripts are used to prepare and flashing single-file firmwares. They are useful to run legacy firmware for debugging purposes or to share modified versions of the firmware.

- `prepare-firmware`: prepare a single-file legacy PRG32 firmware image for publishing. Use `-k` / `--keep-sdkconfig` / `--no-erase-sdkconfig` to disable erasing sdkconfig files.
- `flash-firmware`: flash a published single-file legacy PRG32 firmware image
### Utilities
- `performance`: download the latest in-RAM Performance Test JSON over HTTP or
  through `--port`, so measurements remain available without a network.
  Results contain paired RGB565/indexed measurements; preserve `color_mode`
  when filtering or exporting case summaries. See
  [Performance Test Guide](/docs/performance_test.md) for the complete workflow
  and [Performance Metrics](/docs/measurement/metrics_api.md) for fields.
- `screenshot`: Get screenshot of ESP32C6 over HTTP.
- `wifi-scan`, `wifi-set`, `wifi-forget`: list the networks a running board can
  see and the ones it stores, store and join a network, or erase a stored one,
  over `--port`. `wifi-set` reads the password from a hidden prompt or
  `--password-stdin`, never from the command line. See
  [Stored Networks](network.md#stored-networks).
- `key`: press controller keys on a running board over `--port`, for testing a
  cartridge or the setup menu without touching the buttons. `key UP UP A` presses
  three keys in turn, `key A+B` presses two together, `--hold-ms` and `--gap-ms`
  set the timing, and `--release` ends a long press early. See
  [Key messages](../software/usb_protocol.md#key-messages).
## QEMU Emulator Tasks (`qemu`)
- `build`: build QEMU and generate the flash image. Cleans `./sdkconfig` and `build-qemu/sdkconfig` before configuring to guarantee local default configurations (`profiles/sdkconfig.defaults.local`) are always applied. Use `-k` / `--keep-sdkconfig` (or `--no-erase-sdkconfig`) to disable erasing `sdkconfig` files and preserve existing manual menuconfig changes. Use `-s` / `--skip-target` to decrease compilation time if the ESP32C3 target was already set (e.g. from a previous build).
- `run`: run the QEMU emulator environment
- `upload`: upload a cartridge to QEMU. `--cart-ram-kib` sets the executable
  RAM limit; by default it is read from `build-qemu/sdkconfig`, else 128 KiB
  (see [PRG32 Profiles](profiles.md#matching-host-tools-to-the-profile)).
- `key`: press controller keys on the running emulator, like `esp32c6 key`;
  `--port` defaults to `socket://127.0.0.1:5555`.

Two interactive helpers wrap `cartridge build` and QEMU staging for the
graphics versions of the example games in `examples/games/`:

- `python3 -m prg32.qemu.load_cart <game>|list`: build one game and stage it
  into `build-qemu/qemu_flash.bin`.
- `python3 -m prg32.qemu.start`: a menu that builds, stages, and runs a game.

Both call `inject_cartridge()` in `prg32/qemu/upload_qemu.py`, the same
validation and staging code used by `qemu upload`.

Checked-in cartridge preview videos are recorded from real QEMU execution with
`python3 tools/capture_cartridge_previews.py`. See
[QEMU Screen Emulator](qemu.md#recording-cartridge-previews) for prerequisites,
playfield cropping, UART audio capture, and regeneration details.

For a complete workflow of making a cartridge run on QEMU, see [docs/qemu.md](qemu.md).
## Cartridge Tasks (`cartridge`)
- `build`: build a portable ABI-table `.prg32` cartridge from assembly or C
  source code. Firmware-specific build options are rejected. `--cart-ram-kib`
  sets the executable RAM limit (default 128; use 64 or 32 for a smaller profile,
  see [PRG32 Profiles](profiles.md#matching-host-tools-to-the-profile)).
- `summary`: print the PRG32 cartridge summary (ABI, feature bits, etc.)

For detailed instructions and examples on building cartridges, see [docs/cartridges.md](/docs/software/cartridges.md).

## PRG32 Python Tasks (`python`)
Write cartridges in PRG32 Python, the typed Python subset shared with the
PRG32-Construction-Kit. The SDK reads `game.py` as text (it never runs it),
translates it into commented C or RISC-V assembly, and builds that with the
`cartridge build` code path.

- `new`: write the starter `game.py`.
- `check`: list problems with their line and column (`--json` for tools).
- `to-c` / `to-asm`: write the generated C or assembly.
- `build`: write the generated source beside the cartridge and build a portable
  `.prg32` (`--asm` builds from the assembly).

`python3 -m prg32 p` is ambiguous (`pins`, `python`); type at least `py`. See
[PRG32 Python](../software/python_language.md#commands) for every option.

## ABI Tooling Tasks (`abi`)
The `abi` command manages the `prg32_abi.json` definition and the generated code stubs (`prg32_abi_index.h`, `prg32_abi_hash.h`, etc.).

- `gen`: generate PRG32 portable cartridge ABI C headers (e.g. `prg32_abi_index.h`) from the `prg32_abi.json` definition.
- `check`: verify that the generated PRG32 ABI C headers perfectly match the current `prg32_abi.json` (typically used in CI to ensure developers didn't forget to run `gen`).
## CartridgeStore and Metadata Tasks (`store`)
Interactions with CartridgeStore (metadata attachment, discovery, publishing) are grouped under the `store` command.

- `attach-metadata`: append or replace a PRG32META metadata trailer
- `inspect-metadata`: print the PRG32META trailer summary for a cartridge
- `discover`: find local CartridgeStore instances with mDNS
- `list`: list cartridges from a CartridgeStore
- `download`: download a cartridge from a CartridgeStore
- `publish`: build and publish a cartridge bundle
- `pack-bundle`: pack a flat CartridgeStore zip bundle
- `publish-bundle`: publish a CartridgeStore zip bundle
## Diagnostic Tasks
- `doctor`: check local toolchain prerequisites
- `runtime`: print runtime linker information
- `debug`: query state or send `enable`, `disable`, `pause`, `step`, `resume`,
  and playback `speed` commands to PRG32-QT. See the
  [Web API reference](../software/api.md#prg32-qt-optional-debugger-api).

## GitHub Actions artifacts

The repository workflow builds all six in-tree cartridges with the same unified
CLI used locally. Every successful pull request or push to `main` or
`development-c6` exposes separate downloadable cartridge artifacts for
ESP32-C6 and QEMU. Blackjack and DeviceDemo also include Store bundles. See the
[cartridge CI/CD documentation](../software/cartridges.md#continuous-integration-and-delivery-artifacts)
for validation and retention details.

*(More details will be provided with `--help` on a need basis, e.g., `python3 -m prg32 cartridge build --help`)*
