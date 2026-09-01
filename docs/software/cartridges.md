# PRG32 Cartridges

PRG32 Cartridges allow users to try new games or tools without reflashing the whole firmware. The base package remains backward compatible with earlier PRG32 cartridges. Audio assets are stored in an optional trailing AUDIO block, and store metadata can be appended after the legacy payload as a `PRG32META` trailer.

## Architecture and Format

```text
PRG32 firmware
|-- display, input, audio, score, multiplayer APIs
|-- cartridge loader
|-- executable cartridge RAM
|-- cart0 flash partition
|-- cart1 flash partition
|-- cart2 flash partition
`-- cart3 flash partition

game.prg32
|-- PRG2 cartridge header
|-- linked RV32 code/data payload
|-- optional AUD0 audio block
`-- optional PRG32META metadata trailer
```

The firmware exports the PRG32 API addresses and the cartridge RAM address. `python3 -m prg32` links a game against those addresses and creates a `.prg32` package. The firmware validates the package, persists it in the chosen slot, loads any optional AUDIO block, copies code into executable cartridge RAM, and calls `<game>_init`, `<game>_update`, and `<game>_draw`.

### Flash Layout & Slots

`partitions_prg32.csv` is used by both hardware and QEMU builds:

```text
factory: resident PRG32 firmware
cart0:   uploaded cartridge slot 0
cart1:   uploaded cartridge slot 1
cart2:   uploaded cartridge slot 2
cart3:   uploaded cartridge slot 3
scores:  persistent local scoreboard storage
```

The checked-in classroom firmware supports four persistent slots (`cart0` to `cart3`). Each slot stores a `.prg32` package up to 128 KiB. Only one cartridge is loaded into executable RAM at a time, so additional slots cost flash space, not runtime RAM.

After reset, one stored cartridge starts automatically. When multiple slots contain games, PRG32 enters setup unless a default cartridge has been saved. Use `DEFAULT CARTRIDGE` in setup to choose the slot that should boot automatically, or `RUN CARTRIDGE` to inspect slots.

## Building and Uploading

When you have a running PRG32 configuration, you have multiple options to run your first cartridge:
- Setup the [Cartridge Store](/docs/cartridge_store/cartridge_store.md) and download one of the available cartridges.
- Download or create your own cartridge and upload it via the host tooling.

The PRG32 repository comes with many example cartridge source codes. Here is an example of how to build and upload the cartridge of the game "Asteroids".

### 1. Build a Portable Cartridge
Build a portable cartridge that can run on any PRG32 host (QEMU, physical ESP32-C6) that supports the ABI Table. This example uses the portable ABI table with `--portable`, so it does not need a firmware ELF. 

```bash
python3 -m prg32 cartridge build \
  examples/games/asteroids/graphics/game.S \
  --portable \
  --entry-prefix asteroids_graphics \
  --name asteroids \
  --out build-esp32c6/asteroids.prg32
```

### 2. Upload to Board (ESP32-C6)
Upload it to the physical board over Wi-Fi:

```bash
python3 -m prg32 esp32c6 upload build-esp32c6/asteroids.prg32 --url http://192.168.4.1
```

The firmware stores the cartridge in `cart0` by default and starts running it from the main loop. You can upload to another slot with `--slot cart1`.

### 3. Upload to Emulator (QEMU)
QEMU does not emulate the classroom Wi-Fi upload path. Instead, you stage it directly into the emulator flash image:

```bash
python3 -m prg32 qemu upload build-esp32c6/asteroids.prg32
```

Then start QEMU:
```bash
python3 -m qemu run
```

## Binary Format

### Base Header

The `.prg32` header starts with magic `PRG2` and stores:

- ABI major and minor version
- header size
- flags
- cartridge load address
- code size
- memory size
- init/update/draw offsets
- code payload CRC32
- cartridge name

Current executable cartridge ABI version: major `1`, minor `1`.

`PRG32_CART_FLAG_AUDIO_BLOCK` marks a cartridge that has a trailing AUDIO block.
`PRG32_CART_FLAG_MULTIPLAYER` marks a cartridge that intentionally uses the multiplayer service. 

### Payload

The code payload is linked for `prg32_cart_exec`, copied into executable RAM, and called by the resident firmware. 

#### Assembly

The existing graphics examples already follow the right shape:
- export `<name>_init`, `<name>_update`, and `<name>_draw`
- use normal RV32 calling convention
- save `ra` before calling PRG32 C helpers
- keep stack alignment at 16 bytes around C calls
- keep code/data small enough for the configured cartridge RAM profile

The cartridge linker resolves normal calls such as `call prg32_gfx_clear`.

#### C

C examples use the same entry shape and the same PRG32 ABI:

```c
void platformer_c_init(void);
void platformer_c_update(void);
void platformer_c_draw(void);
```

Build them with the same tool. The builder detects `.c` sources and compiles them as small freestanding C modules. Keep C cartridges small and avoid standard-library calls. Use the helpers in `prg32.h` for display, input, audio, sprites, playfields, and platform physics.

### Optional AUDIO Block

When present, the AUDIO block follows immediately after the code payload:

```text
PRG2 header
code/data payload
AUD0 audio block
```

The firmware loads the AUDIO block before calling `<game>_init`, so a cartridge can immediately call `prg32_audio_play_sample(0, 255, 1024)` when sample `0` is defined in the block. Cartridges without AUDIO blocks remain valid. AUDIO blocks are stored after the code payload and count against cartridge package size and partition size, not cartridge executable RAM.

The AUDIO block header stores offsets to:
- sample descriptors
- instrument descriptors
- track descriptors
- tracker events
- raw sample bytes

#### Sample Descriptor

```c
typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_end;
    uint16_t base_note;
    uint8_t flags;
    uint8_t reserved;
} prg32_sample_desc_t;
```

Flag bit 0 enables looping. Source sample bytes are unsigned 8-bit PCM mono.

#### Instrument Descriptor

```c
typedef struct {
    uint16_t sample_id;
    uint8_t default_volume;
    int8_t default_pan;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
} prg32_instrument_desc_t;
```

The ADSR fields are reserved for future envelope lessons.

#### Track Events

```c
typedef struct {
    uint8_t delta_ticks;
    uint8_t command;
    uint8_t arg0;
    uint8_t arg1;
} prg32_audio_event_t;
```

Commands include `NOTE_ON`, `NOTE_OFF`, `SET_VOLUME`, `SET_PAN`, `SET_TEMPO`, `PLAY_SAMPLE`, `JUMP`, and `END`.

#### Asset Packing Pipeline

```bash
python3 tools/wav2prg32sample.py input.wav --rate 22050 --out build/input.raw
python3 tools/prg32audio_pack.py audio.json --out build/audio.block
python3 -m prg32 cartridge build game.S \
  --portable \
  --entry-prefix mygame \
  --audio-block build/audio.block \
  --out build-esp32c6/mygame.prg32
```

### Optional Metadata Trailer

Store-ready cartridges append a TLV metadata trailer after the legacy payload (and optional audio block). All integer fields are little endian.

```text
legacy payload:
  PRG2 header
  linked code/data bytes
  optional AUD0 audio block

metadata trailer:
  magic[9]      = "PRG32META"
  version       = uint8, currently 1
  entry_count   = uint16
  trailer_size  = uint32, including this header and every TLV entry

entries:
  type[4]       = ASCII block type
  length        = uint32
  value[length] = raw block bytes
```

Known TLV block types:

| Type | Meaning |
| --- | --- |
| `META` | UTF-8 JSON metadata conforming to `prg32-metadata-1.0` |
| `ICON` | icon image bytes, preferably PNG |
| `SCRN` | optional screenshot bytes, preferably PNG or JPEG |
| `SIGN` | optional signature bytes or UTF-8 JSON signature object |
| `COLO` | UTF-8 JSON colophon conforming to `prg32-colophon-1.0` |

Unknown block types are allowed and must be preserved by tools that rewrite a trailer. Malformed trailers fail safely: parsers validate the header length, entry count, and every block length before exposing decoded blocks.

Old firmware and tooling read only the legacy sizes from the `PRG2` header and ignore trailing bytes. PRG32 firmware recognizes the metadata trailer length when reading stored slots, so monolithic images stay intact across reset even though the game code loader ignores metadata bytes.

#### Metadata ABI

The `META` block uses ABI `prg32-metadata-1.0`.

```json
{
  "abi": "prg32-metadata-1.0",
  "id": "org.example.game",
  "title": "Game title",
  "version": "1.0.0",
  "summary": "Short description",
  "description": "Longer description",
  "authors": [
    {
      "name": "Author name",
      "email": "optional@example.com",
      "url": "https://optional.example"
    }
  ],
  "license": "MIT",
  "homepage": "https://example.org",
  "repository": "https://github.com/example/game",
  "tags": ["arcade", "riscv", "education"],
  "created_at": "2026-05-29T00:00:00Z",
  "updated_at": "2026-05-29T00:00:00Z",
  "runtime": {
    "platform": "PRG32",
    "isa": "RV32I",
    "architecture": "esp32c6",
    "architectures": [
      {
        "id": "esp32c6",
        "label": "ESP32-C6 hardware",
        "target": "esp32c6",
        "display": "ili9341",
        "isa": "RV32I"
      },
      {
        "id": "qemu",
        "label": "QEMU virtual screen",
        "target": "esp32c3",
        "display": "qemu-rgb",
        "isa": "RV32I"
      }
    ],
    "min_firmware": "optional"
  },
  "assets": {
    "icon": {
      "block": "ICON",
      "mime": "image/png"
    },
    "screenshot": {
      "block": "SCRN",
      "mime": "image/png",
      "optional": true
    }
  },
  "signature": {
    "block": "SIGN",
    "optional": true
  },
  "colophon": {
    "block": "COLO",
    "abi": "prg32-colophon-1.0"
  }
}
```

Required metadata fields are `abi`, `id`, `title`, and `version`. `authors` and `tags` must be arrays when present. Unknown fields are allowed for forward compatibility. The builder serializes JSON blocks deterministically with sorted keys and compact separators. Cartridges without `SCRN` or `SIGN` are valid. Cartridges without `COLO` are also valid, but the builder warns because the Cartridge Store prefers colophon-complete cartridges. See [colophon_abi.md](colophon_abi.md) for the colophon ABI documentation.

#### Architecture Variants

A `.prg32` file contains one linked executable image. ESP32-C6 hardware and the QEMU graphics workflow can use different target metadata or assets, so the Cartridge Store manages them as separate architecture variants of the same game/version:

| Architecture id | Build target | Typical output |
| --- | --- | --- |
| `esp32c6` | physical ESP32-C6 firmware | `build-esp32c6/game.prg32` |
| `qemu` | ESP32-C3 QEMU graphics firmware | `build-qemu/game.prg32` |

Build each variant as a portable cartridge, then attach metadata with the matching `--architecture`.

```bash
python3 -m prg32 attach-metadata \
  build-esp32c6/game.prg32 \
  --metadata metadata.json \
  --icon icon.png \
  --screenshot screenshot.png \
  --colophon colophon.json \
  --architecture esp32c6 \
  --out dist/game-esp32c6.prg32

python3 -m prg32 attach-metadata \
  build-qemu/game.prg32 \
  --metadata metadata.json \
  --icon icon.png \
  --colophon colophon.json \
  --architecture qemu \
  --out dist/game-qemu.prg32
```

## Inspecting Cartridges

You can inspect a built cartridge via:

```bash
python3 -m prg32 cartridge summary CARTRIDGE
```

The summary shows ABI major/minor, ABI hash, import model, and required or optional feature bits. ABI hash mismatches, missing required features, and incompatible legacy cartridges are rejected by the runtime, store download path, QEMU staging path, and HTTP upload tool with a diagnostic message.

To verify a monolithic cartridge (including metadata):

```bash
python3 -m prg32 store inspect-metadata dist/game-esp32c6.prg32
```

## Multiplayer Cartridges

A cartridge opts in to multiplayer by calling `prg32_multiplayer_join(signature, flags)` from its game code. The build tool can also mark the package header with `PRG32_CART_FLAG_MULTIPLAYER`:

```bash
python3 -m prg32 cartridge build \
  examples/games/pong/c/game.c \
  --portable \
  --entry-prefix pong_c \
  --multiplayer \
  --name pong-mp \
  --out build-esp32c6/pong-mp.prg32
```

Use the same signature string for compatible cartridge builds. The WebSocket server groups only matching signatures, so a `pong-v1` cartridge never receives state from a `breakout-v1` cartridge or an incompatible `pong-v2` revision.

On ESP32-C6, multiplayer uses Wi-Fi station mode and the standalone Node.js [MultiplayerServer](https://github.com/riscv-prg32/MultiplayerServer). QEMU keeps the same API available with a local offline stub so the cartridge still builds and runs on the desktop path.

## Limits & Profiles

This is intentionally a classroom loader, not a general dynamic linker.

- Cartridge package size is 128 KiB.
- For details on executable Cartridge RAM limits and expanding the cartridge limits using profiles, see the [PRG32 Profiles](/docs/usage/profiles.md) documentation.

## Development Guide

> [!IMPORTANT]
> This information is only intended for developers of the PRG32 framework.

### Cartridge Runtime Details

- Uploadable cartridges run from `prg32_cart_exec` and are linked for that
  runtime address. Rebuild cartridges whenever the resident firmware changes.
- Keep `PRG32_CART_RAM_SIZE` small enough for classroom examples unless the
  partition/RAM plan is intentionally revised.
- Keep `partitions_prg32.csv`, `sdkconfig.defaults`, and `sdkconfig.defaults.qemu`
  in sync when changing cartridge slots.
