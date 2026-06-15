# PRG32 Cartridge Format

PRG32 cartridges are native RV32 code packages loaded by the resident firmware.
The base package remains compatible with earlier PRG32 cartridges. Audio assets
are stored in an optional trailing AUDIO block, and store metadata can be
appended after the legacy payload as a `PRG32META` trailer.

## Base Header

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
`PRG32_CART_FLAG_MULTIPLAYER` marks a cartridge that intentionally uses the
multiplayer service. The game still calls `prg32_multiplayer_join()` at runtime
with its cartridge signature.

## Payload

The code payload is linked for `prg32_cart_exec`, copied into executable RAM,
and called by the resident firmware.

## Optional AUDIO Block

When present, the AUDIO block follows immediately after the code payload:

```text
PRG2 header
code/data payload
AUD0 audio block
```

The AUDIO block header stores offsets to:

- sample descriptors
- instrument descriptors
- track descriptors
- tracker events
- raw sample bytes

## Sample Descriptor

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

## Instrument Descriptor

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

## Track Events

```c
typedef struct {
    uint8_t delta_ticks;
    uint8_t command;
    uint8_t arg0;
    uint8_t arg1;
} prg32_audio_event_t;
```

Commands include `NOTE_ON`, `NOTE_OFF`, `SET_VOLUME`, `SET_PAN`, `SET_TEMPO`,
`PLAY_SAMPLE`, `JUMP`, and `END`.

## Asset Packing Pipeline

```bash
python3 tools/wav2prg32sample.py input.wav --rate 22050 --out build/input.raw
python3 tools/prg32audio_pack.py audio.json --out build/audio.block
python3 -m prg32 build game.S \
  --portable \
  --entry-prefix mygame \
  --audio-block build/audio.block \
  --out build-esp32c6/mygame.prg32
```

The firmware loads AUDIO assets before calling the cartridge init function, so
student code can trigger sample id `0` immediately when the block defines it.

## Optional Metadata Trailer

Store-ready cartridges append a TLV metadata trailer after the legacy payload:

```text
PRG2 header
code/data payload
optional AUD0 audio block
PRG32META metadata trailer
```

The trailer starts with magic `PRG32META`, version `1`, an entry count, and the
total trailer length. Each TLV entry has a four-byte ASCII type and a little
endian length. Known block types are `META`, `ICON`, `SCRN`, `SIGN`, and `COLO`.
Unknown blocks are allowed and should be preserved by tools.

`META` contains `prg32-metadata-1.0` JSON. `COLO` contains
`prg32-colophon-1.0` JSON. The game colophon is shown after the cartridge is
activated, before the player starts a new play.

See [cartridge_metadata.md](cartridge_metadata.md) and
[colophon_abi.md](colophon_abi.md) for the formal ABI documentation.
