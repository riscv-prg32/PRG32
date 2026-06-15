# PRG32 Unified Tooling

The PRG32 project now ships with a unified Python package, `prg32`, which replaces the older collection of monolithic scripts like `tools/prg32_game.py`, `tools/prg32_prepare_legacy_firmware.py`, and `tools/prg32_flash_legacy_firmware.py`.

The new unified tool organizes commands into functional subcommands, improving maintainability, speed, and developer experience.

## Usage Overview

You run the new tooling as a module via Python:
```bash
python3 -m prg32 <command> [subcommand] [options]
```

### Main Commands

- **`build-cartridge`**: Build a `.prg32` cartridge from assembly or C source code.
- **`doctor`**: Check the PRG32 environment, paths, ESP-IDF tools, and dependencies.
- **`abi`**: Manage the Portable ABI.
- **`qemu`**: QEMU testing and execution environments.
- **`esp32c6`**: Flashing and building operations for physical hardware targets.
- **`store`**: Interactions with a PRG32 CartridgeStore.

---

## Cartridge Building

The build command has moved from `tools/prg32_game.py build` to `python3 -m prg32 build-cartridge`.

```bash
python3 -m prg32 build-cartridge examples/games/asteroids/graphics/game.S \
    --out build-esp32c6/asteroids.prg32 \
    --name asteroids \
    --entry-prefix asteroids \
    --target esp32c6
```

To build a **portable** cartridge that can run on any PRG32 host (QEMU, physical ESP32-C6) that supports the ABI Table, use `--portable`:

```bash
python3 -m prg32 build-cartridge examples/games/pacman/c/game.c \
    --out build-portable-examples/pacman.prg32 \
    --name pacman \
    --entry-prefix pacman_c \
    --portable
```

---

## ESP32-C6 Operations

All commands interacting with physical ESP32-C6 hardware fall under the `esp32c6` group.

### Upload & Run Cartridges
Instead of `tools/prg32_game.py upload`, you use `python3 -m prg32 esp32c6 upload`:

```bash
python3 -m prg32 esp32c6 upload build-esp32c6/asteroids.prg32 --url http://192.168.4.1
```

You can upload and immediately run using `upload-and-run`. You can also just `run` a cartridge in a specific slot.

### Firmware Operations

The `esp32c6` subcommand also wraps ESP-IDF functions:
- `python3 -m prg32 esp32c6 build` 
- `python3 -m prg32 esp32c6 flash` 
- `python3 -m prg32 esp32c6 build-and-flash`
- `python3 -m prg32 esp32c6 reset`
- `python3 -m prg32 esp32c6 erase-flash`

### Legacy Firmware Tools

The scripts for preparing and flashing legacy single-file firmware (used heavily in classes before portable cartridges) have been merged into this group:
- `python3 -m prg32 esp32c6 prepare-legacy`
- `python3 -m prg32 esp32c6 flash-legacy <manifest>`

---

## QEMU Operations

QEMU commands simulate the PRG32 environment locally.

### Uploading to QEMU

Staging a cartridge directly into QEMU's simulated Flash uses `qemu upload`:

```bash
python3 -m prg32 qemu upload build-qemu/asteroids.prg32
```

### Launching QEMU

You can launch the QEMU simulator directly from the CLI:

```bash
python3 -m prg32 qemu launch
```

---

## Store Metadata & Publishing

Interactions with CartridgeStore (metadata attachment, discovery, publishing) are grouped under the `store` command.

- **`python3 -m prg32 store discover`**
- **`python3 -m prg32 store list`**
- **`python3 -m prg32 store attach-metadata`**
- **`python3 -m prg32 store summary`** (Previously `tools/prg32_game.py inspect-metadata`)
- **`python3 -m prg32 store publish`**
- **`python3 -m prg32 store download`**

---

## ABI Tools

The `abi` command manages the `prg32_abi.json` definition and the generated code stubs (`prg32_abi_index.h`, `prg32_abi_hash.h`, etc.).

- **`python3 -m prg32 abi gen`**: Generates C/Python headers and files.
- **`python3 -m prg32 abi check`**: Validates that generated ABI code is synchronized with the JSON file.

These were previously `tools/prg32_abi_gen.py`.
