# PRG32 Unified Tooling

The PRG32 project ships with a unified Python package, `prg32`.

You run the tooling as a module via Python:
```bash
python3 -m prg32 <command> [subcommand] [options]
```
## Main Commands
- `esp32c6`: ESP32C6 SoC tasks
- `qemu`: QEMU emulator tasks
- `cartridge`: Cartridge tasks (build, summary, etc.)
- `abi`: ABI tooling tasks
- `store`: CartridgeStore and metadata tasks
- `doctor`: Check local toolchain prerequisites
- `runtime`: Print runtime linker information

## ESP32C6 SoC Tasks (`esp32c6`)
All commands interacting with physical ESP32-C6 hardware fall under the `esp32c6` group.

### Firmware Operations
- `build`: build the ESP32C6 firmware. Use the `--skip-target` option to decrease compilation time if the ESP32C6 target was already set (e.g. from a previous build).
- `flash`: flash the ESP32C6 image. Does not do anything when a cartridge is running!
    - `build-and-flash`: combination of `build` and `flash` commands for convenience. Also supports the `--skip-target` option.
- `erase-flash`: erase flash of ESP32C6. It is sometimes useful to completely erase the flash for debugging purposes.
    - `reset`: erase flash and re-flash. Combination of `erase-flash` and `flash`, for convenience.
- `memory`: get static and dynamic memory analysis of the ESP32C6 SoC (See [docs/memory.md](/docs/hardware/memory.md)
### Upload & Run Cartridges
- `upload`: upload a cartridge to the ESP32C6 SoC over HTTP,
- `run`: instruct the ESP32C6 over HTTP to run a previously loaded cartridge on a selected slot. Does not work when a cartridge is running (?).
    - `upload-and-run`: combination of `upload` and `run` for convenience.
### Firmware Tools
These scripts are used to prepare and flashing single-file firmwares. They are useful to run legacy firmware for debugging purposes or to share modified versions of the firmware.

- `prepare-firmware`: prepare a single-file legacy PRG32 firmware image for publishing
- `flash-firmware`: flash a published single-file legacy PRG32 firmware image
### Utilities
- `performance`: download the latest in-RAM Performance Test JSON over HTTP.
  Results contain paired RGB565/indexed measurements; preserve `color_mode`
  when filtering or exporting samples. See
  [Performance Metrics](/docs/measurement/metrics_api.md).
- `screenshot`: Get screenshot of ESP32C6 over HTTP.
## QEMU Emulator Tasks (`qemu`)
- `build`: build QEMU and generate the flash image. Use the `--skip-target` option to decrease compilation time if the ESP32C3 target was already set (e.g. from a previous build).
- `run`: run the QEMU emulator environment
- `build-and-run`: build QEMU, generate flash image, and run the emulator. Also supports the `--skip-target` option.
- `upload`: upload a cartridge to QEMU

For a complete workflow of making a cartridge run on QEMU, see [docs/qemu.md](qemu.md).
## Cartridge Tasks (`cartridge`)
- `build`: build a `.prg32` cartridge from assembly or C source code.
- `summary`: print the PRG32 cartridge summary (ABI, feature bits, etc.)

For detailed instructions and examples on building cartridges, see [docs/cartridges.md](/docs/software/cartridges.md).

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

## GitHub Actions artifacts

The repository workflow builds Blackjack and DeviceDemo with the same unified
CLI used locally. Every successful pull request or push to `main` or
`development-c6` exposes separate downloadable cartridge artifacts containing
the ESP32-C6 variant, QEMU variant, and store bundle. See the
[cartridge CI/CD documentation](../software/cartridges.md#continuous-integration-and-delivery-artifacts)
for validation and retention details.

*(More details will be provided with `--help` on a need basis, e.g., `python3 -m prg32 cartridge build --help`)*
