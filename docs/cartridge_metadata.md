# PRG32 Cartridge Metadata Trailer

PRG32 cartridges remain backward compatible with the original `.prg32` layout.
The executable payload is still the legacy header, linked RV32 code/data, and
optional `AUD0` audio block. Store metadata is appended after that payload as a
`PRG32META` trailer.

Old firmware and tooling read only the legacy sizes from the `PRG2` header and
ignore trailing bytes. New tooling computes the canonical legacy payload end and
then looks for the metadata trailer.

## Trailer Binary Format

All integer fields are little endian.

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

Unknown block types are allowed and must be preserved by tools that rewrite a
trailer. Malformed trailers fail safely: parsers validate the header length,
entry count, and every block length before exposing decoded blocks.

## Metadata ABI

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

Required metadata fields are `abi`, `id`, `title`, and `version`. `authors` and
`tags` must be arrays when present. Unknown fields are allowed for forward
compatibility.

## Architecture Variants

A `.prg32` file contains one linked executable image. ESP32-C6 hardware and the
QEMU graphics workflow can use different target metadata or assets, so the
Cartridge Store manages them as separate architecture variants of the same
game/version:

| Architecture id | Build target | Typical output |
| --- | --- | --- |
| `esp32c6` | physical ESP32-C6 firmware | `build-esp32c6/game.prg32` |
| `qemu` | ESP32-C3 QEMU graphics firmware | `build-qemu/game.prg32` |

Build each variant as a portable cartridge, then attach metadata with the
matching `--architecture`.

```bash
python3 tools/prg32_game.py attach-metadata \
  build-esp32c6/game.prg32 \
  --metadata metadata.json \
  --icon icon.png \
  --screenshot screenshot.png \
  --colophon colophon.json \
  --architecture esp32c6 \
  --out dist/game-esp32c6.prg32

python3 tools/prg32_game.py attach-metadata \
  build-qemu/game.prg32 \
  --metadata metadata.json \
  --icon icon.png \
  --colophon colophon.json \
  --architecture qemu \
  --out dist/game-qemu.prg32
```

Use `inspect-metadata` to verify a monolithic cartridge:

```bash
python3 tools/prg32_game.py inspect-metadata dist/game-esp32c6.prg32
```

The builder serializes JSON blocks deterministically with sorted keys and compact
separators. Cartridges without `SCRN` or `SIGN` are valid. Cartridges without
`COLO` are also valid, but the builder warns because the Cartridge Store prefers
colophon-complete cartridges.

## Compatibility Notes

- Legacy cartridges without `PRG32META` parse as normal cartridges with no
  metadata trailer.
- Existing loaders keep validating and copying only the executable code payload.
- The optional `AUD0` block remains immediately after the code payload.
- The metadata trailer follows `AUD0` when audio is present.
- PRG32 firmware recognizes the metadata trailer length when reading stored
  slots, so monolithic images stay intact across reset even though the game code
  loader ignores metadata bytes.
