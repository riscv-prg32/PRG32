# PRG32 Cartridge Colophon ABI

The standard cartridge colophon ABI is `prg32-colophon-1.0`. It is stored as
UTF-8 JSON in the `COLO` metadata trailer block.

The game colophon is shown after the cartridge is activated, before the player
starts a new play.

## JSON Shape

```json
{
  "abi": "prg32-colophon-1.0",
  "title": "Game title",
  "subtitle": "Optional subtitle",
  "version": "1.0.0",
  "release_date": "2026-05-29",
  "developer": {
    "name": "Developer or studio",
    "url": "https://example.org"
  },
  "authors": [
    {
      "role": "game design",
      "name": "Name"
    },
    {
      "role": "programming",
      "name": "Name"
    },
    {
      "role": "art",
      "name": "Name"
    },
    {
      "role": "music",
      "name": "Name"
    }
  ],
  "license": "MIT",
  "copyright": "Copyright 2026 Developer",
  "acknowledgements": [
    "Thanks to the PRG32 community"
  ],
  "dedication": "Optional dedication",
  "content_notice": "Optional content notice",
  "controls": [
    {
      "input": "A",
      "action": "Jump"
    },
    {
      "input": "B",
      "action": "Fire"
    }
  ],
  "start_prompt": "Press START to play"
}
```

## Validation Rules

- `abi`, `title`, `version`, and `developer.name` are required.
- `authors` may be empty but must be an array.
- `controls` may be empty but must be an array.
- Unknown fields are allowed for forward compatibility.
- Text is UTF-8.
- Keep the colophon compact enough for the embedded 320x240 display.

The host builder fills missing `authors` and `controls` with empty arrays before
validation. Store software should reject a colophon with the wrong ABI or
missing required display fields.

## Runtime Flow

Firmware setup screens and host launchers should follow this activation flow:

```c
activate_cartridge(slot_id);

if (cartridge_has_colophon(slot_id)) {
    prg32_colophon_t colophon;
    prg32_load_colophon(slot_id, &colophon);
    prg32_show_colophon(&colophon);

    wait_until_player_confirms_start();
} else if (cartridge_has_metadata(slot_id)) {
    prg32_show_minimal_metadata_start_screen(slot_id);
    wait_until_player_confirms_start();
}

start_new_play(slot_id);
```

Legacy cartridges remain valid. When no `COLO` block is present, firmware may
continue directly or show a minimal fallback using the metadata title, version,
and developer when those fields are available.
