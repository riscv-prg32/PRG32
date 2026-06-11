# PRG32 Uploadable Game Cartridges

PRG32 can be flashed once as a resident runtime. After that, students can build
small native RISC-V game cartridges and upload them without reflashing the whole
firmware.

The same cartridge format is used on:

- real ESP32-C6 hardware, uploaded over the PRG32 Wi-Fi HTTP API
- QEMU, staged into the emulator flash image

## Mental Model

```text
PRG32 firmware
|-- display, input, audio, score, multiplayer APIs
|-- cartridge loader
|-- executable cartridge RAM
|-- cart0 flash partition
`-- cart1 flash partition

game.prg32
|-- PRG32 cartridge header
|-- linked RV32 code/data payload
|-- optional AUDIO block
`-- optional PRG32META metadata trailer
```

The firmware exports the PRG32 API addresses and the cartridge RAM address.
`tools/prg32_game.py` links a game against those addresses and creates a
`.prg32` package. The firmware validates the package, persists it in the chosen
slot, loads any optional AUDIO block, copies code into executable cartridge RAM,
and calls:

- `<game>_init`
- `<game>_update`
- `<game>_draw`

## Flash Layout

`partitions_prg32.csv` is used by both hardware and QEMU builds:

```text
factory: resident PRG32 firmware
cart0:   uploaded cartridge slot 0
cart1:   uploaded cartridge slot 1
```

This partition table is selected by `sdkconfig.defaults` and
`sdkconfig.defaults.qemu`.

## Slot Count

The checked-in classroom firmware supports two persistent slots:
`cart0` and `cart1`. Only one cartridge is loaded into executable RAM at a time,
so additional slots cost flash space, not runtime RAM.

With the current 4 MB flash image layout, a third 512 KiB slot can fit after
`cart1` if the partition table and loader slot metadata are extended. On an
8 MB ESP32-C6 module, the same 512 KiB slot size can support about eleven total
slots after updating the ESP-IDF flash size, `partitions_prg32.csv`,
`PRG32_CART_SLOT_COUNT`, the slot labels/subtypes in the cartridge loader, and
the host tooling/docs. Keep fewer slots for labs unless the course explicitly
needs a cartridge library; two slots are easier for students to reason about.

## Hardware Workflow

Flash the resident firmware one time:

```bash
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor
```

The setup screen can start a small Wi-Fi access point:

```text
SSID:     PRG32
Password: prg32game
URL:      http://192.168.4.1
```

Build a cartridge from an assembly or C example. This example uses the portable ABI table, so it does not need a firmware ELF:

```bash
python3 tools/prg32_game.py build \
  examples/games/asteroids/graphics/game.S \
  --portable \
  --entry-prefix asteroids_graphics \
  --name asteroids \
  --out build-esp32c6/asteroids.prg32
```

Upload it to the board:

```bash
python3 tools/prg32_game.py upload build-esp32c6/asteroids.prg32 --url http://192.168.4.1
```

The upload tool reads `/api/runtime` before deployment and rejects incompatible
cartridges before sending them. The error message names the incompatible ABI
major, ABI hash, missing feature bits, or legacy load address. The firmware
performs the same ABI contract check when it receives or loads a cartridge.

The firmware stores the cartridge in `cart0` by default and starts running it
from the main loop. Upload to `cart1` with:

```bash
python3 tools/prg32_game.py upload build-esp32c6/asteroids.prg32 --slot cart1 --url http://192.168.4.1
```

After reset, one stored cartridge starts automatically. When both slots contain
games, PRG32 enters setup unless a default cartridge has been saved. Use
`DEFAULT CARTRIDGE` in setup to choose the slot that should boot automatically,
or `RUN CARTRIDGE` to launch a slot immediately.

## Multiplayer Cartridges

A cartridge opts in to multiplayer by calling
`prg32_multiplayer_join(signature, flags)` from its game code. The build tool
can also mark the package header with `PRG32_CART_FLAG_MULTIPLAYER`:

```bash
python3 tools/prg32_game.py build \
  examples/games/pong/c/game.c \
  --portable \
  --entry-prefix pong_c \
  --multiplayer \
  --name pong-mp \
  --out build-esp32c6/pong-mp.prg32
```

Use the same signature string for compatible cartridge builds. The WebSocket
server groups only matching signatures, so a `pong-v1` cartridge never receives
state from a `breakout-v1` cartridge or an incompatible `pong-v2` revision.

On ESP32-C6, multiplayer uses Wi-Fi station mode and the standalone Node.js
[MultiplayerServer](https://github.com/riscv-prg32/MultiplayerServer). QEMU
keeps the same API available with a local offline stub so the cartridge still
builds and runs on the desktop path.

## Portable Build Workflow

Portable cartridges use the generated ABI table contract and do not need a
firmware ELF or runtime HTTP query at build time:

```bash
python3 tools/prg32_game.py build \
  examples/games/asteroids/graphics/game.S \
  --portable \
  --entry-prefix asteroids_graphics \
  --name asteroids \
  --out build-esp32c6/asteroids.prg32
```

Useful runtime endpoint for diagnostics:

```bash
curl http://192.168.4.1/api/runtime
```

## QEMU Workflow

Build the resident QEMU firmware:

```bash
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu build
```

Build a portable cartridge for QEMU staging:

```bash
python3 tools/prg32_game.py build \
  examples/games/asteroids/graphics/game.S \
  --portable \
  --entry-prefix asteroids_graphics \
  --name asteroids \
  --out build-qemu/asteroids.prg32
```

Stage it into the QEMU flash image:

```bash
python3 tools/prg32_game.py upload-qemu \
  build-qemu/asteroids.prg32 \
  --flash build-qemu/qemu_flash.bin
```

If `build-qemu/qemu_flash.bin` is missing, start QEMU once so ESP-IDF creates
the flash image, quit QEMU, and run the staging command again.

Then start QEMU:

```bash
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

QEMU does not emulate the classroom Wi-Fi upload path. Instead, it uses the same
cartridge package and writes it into `cart0` by default, or `cart1` when
`upload-qemu --slot cart1` is used.

## Store-Ready Metadata

The optional `PRG32META` trailer turns a cartridge into a monolithic store
artifact containing metadata, an icon, an optional screenshot, an optional
signature, and an optional colophon.

Create the executable cartridge first, then append metadata:

```bash
python3 tools/prg32_game.py attach-metadata \
  build-esp32c6/asteroids.prg32 \
  --metadata metadata.json \
  --icon icon.png \
  --screenshot screenshot.png \
  --colophon colophon.json \
  --architecture esp32c6 \
  --out dist/asteroids-esp32c6.prg32
```

Inspect the trailer:

```bash
python3 tools/prg32_game.py inspect-metadata dist/asteroids-esp32c6.prg32
```

A `.prg32` artifact contains one linked cartridge architecture. Build and
publish separate artifacts for physical ESP32-C6 hardware and the QEMU graphics
workflow:

```bash
# Physical board variant.
python3 tools/prg32_game.py build ... \
  --portable \
  --out build-esp32c6/game.prg32

# QEMU variant.
python3 tools/prg32_game.py build ... \
  --portable \
  --out build-qemu/game.prg32
```

Use `--architecture esp32c6` for the physical build and `--architecture qemu`
for the emulator build. The Cartridge Store groups those artifacts by metadata
`id` and `version`, then offers the correct architecture to firmware or QEMU
clients.

Build every checked-in game and feature example as a portable cartridge and
prepare flat CartridgeStore bundles:

```bash
python3 tools/prg32_build_portable_examples.py --clean
```

The output directory defaults to `build-portable-examples`. For each example the
script writes a `.prg32` file plus `esp32c6` and `qemu` publishing bundles.

See [cartridge_metadata.md](cartridge_metadata.md) for the binary trailer and
metadata ABI, [colophon_abi.md](colophon_abi.md) for the colophon ABI, and
[setup_mode_cartridge_store.md](setup_mode_cartridge_store.md) for the
setup-mode integration contract.

## Downloading from a CartridgeStore

CartridgeStore provides a shared classroom catalog for `.prg32` artifacts. See
[cartridge_store.md](cartridge_store.md) for setup, publishing, and
troubleshooting details.

Two installation paths are available:

- On-device: enter setup, open `BROWSE STORE`, choose a compatible game, and
  download it into `cart0` or `cart1`.
- Host tool: run `python3 tools/prg32_game.py store-download ...` and then
  upload the downloaded `.prg32` with `python3 tools/prg32_game.py upload ...`.

Both paths validate the ABI contract. A cartridge with the wrong ABI major,
wrong ABI hash, unsupported required feature bits, unsupported import model, or
wrong legacy load address is rejected with a clear diagnostic instead of being
deployed silently.

## HTTP API

### Runtime Information

```http
GET /api/runtime
```

Returns:

- cartridge ABI version
- cartridge RAM load address
- cartridge RAM size
- PRG32 API import addresses
- currently loaded cartridge metadata

### List Games

```http
GET /api/games
```

Returns the `cart0` and `cart1` cartridge states.

### Upload Game

```http
POST /api/games?slot=cart0
Content-Type: application/octet-stream

<game.prg32 bytes>
```

Validates and stores the cartridge without starting it. Use `slot=cart1` for
the second slot.

### Select Stored Game

```http
POST /api/games/select?slot=cart0
```

Loads and starts the stored cartridge in the selected slot.

### Screenshot

```http
GET /api/screenshot.bmp
```

Returns the current 320x240 PRG32 framebuffer as a 24-bit BMP image. This
captures the full physical screen, including splash/setup screens, the centered
320x200 game viewport, and the upper/lower status bands.

Example:

```bash
curl http://192.168.4.1/api/screenshot.bmp --output screenshot.bmp
```

The BMP is generated from the same normalized RGB565 framebuffer path used by
the ILI9341 hardware backend and the QEMU RGB backend.

## Cartridge Assembly Rules

The existing graphics examples already follow the right shape:

- export `<name>_init`, `<name>_update`, and `<name>_draw`
- use normal RV32 calling convention
- save `ra` before calling PRG32 C helpers
- keep stack alignment at 16 bytes around C calls
- keep code/data small enough for the configured cartridge RAM profile

The cartridge linker resolves normal calls such as:

```asm
call prg32_gfx_clear
call prg32_input_read
call prg32_audio_beep
```

## Cartridge C Rules

C examples use the same entry shape and the same PRG32 ABI:

```c
void platformer_c_init(void);
void platformer_c_update(void);
void platformer_c_draw(void);
```

Build them with the same tool. The builder detects `.c` sources and compiles
them as small freestanding C modules:

```bash
python3 tools/prg32_game.py build \
  examples/games/platformer/c/game.c \
  --portable \
  --entry-prefix platformer_c \
  --name platformer-c \
  --out build-esp32c6/platformer-c.prg32
```

Keep C cartridges small and avoid standard-library calls. Use the helpers in
`prg32.h` for display, input, audio, sprites, playfields, and platform physics.
For a fuller C cartridge, build `examples/games/raycaster/c/game.c` with
`--entry-prefix raycaster_c`; it uses fixed-point tables and direct PRG32
drawing calls instead of libc or floating-point helpers.
For layered graphics, build `examples/games/wing_commander/c/game.c` with
`--entry-prefix wing_commander_c`; it demonstrates dual playfields in a
playable cockpit game.

## Cartridge AUDIO Blocks

Cartridges can include a trailing AUDIO block marked by
`PRG32_CART_FLAG_AUDIO_BLOCK`. This block carries sample descriptors,
instrument descriptors, tracker events, and raw unsigned 8-bit sample bytes.

Pack an AUDIO block:

```bash
python3 tools/wav2prg32sample.py jump.wav --rate 22050 --out build/jump.raw
python3 tools/prg32audio_pack.py audio.json --out build/audio.block
```

Attach it to a cartridge:

```bash
python3 tools/prg32_game.py build \
  examples/games/asteroids/graphics/game.S \
  --portable \
  --entry-prefix asteroids_graphics \
  --audio-block build/audio.block \
  --name asteroids-audio \
  --out build-esp32c6/asteroids-audio.prg32
```

The firmware loads the AUDIO block before calling `<game>_init`, so a cartridge
can immediately call `prg32_audio_play_sample(0, 255, 1024)` when sample `0` is
defined in the block. Cartridges without AUDIO blocks remain valid.

## Limits

This is intentionally a classroom loader, not a general dynamic linker.

- Cartridges are linked for one PRG32 firmware build.
- If the firmware is rebuilt, rebuild the cartridges.
- Cartridge package size is 32 KiB.
- Cartridge RAM is selected by `CONFIG_PRG32_CART_RAM_PROFILE`: physical
  classroom builds default to 32 KiB, while QEMU and extended builds use
  64 KiB unless a custom profile is selected.
- AUDIO blocks are stored after the code payload and count against cartridge
  package size and partition size, not cartridge executable RAM.
- Two flash slots, `cart0` and `cart1`, are available. Only one cartridge is
  loaded into executable RAM at a time.
- QEMU staging requires QEMU to be stopped before patching `qemu_flash.bin`.
