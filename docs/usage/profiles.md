# PRG32 Profiles

PRG32 uses different RAM profiles to adjust how much internal executable RAM the resident firmware reserves for uploadable cartridges. The active profile dictates the cartridge size limits and the available drawing features.

## Default Profiles

- **Classroom Profile (32 KiB)**: An optional smaller window for builds that
  prioritize resident-runtime heap.
- **Extended Profile (64 KiB)**: The default for physical ESP32-C6 and QEMU
  builds. The default stored package limit is also 64 KiB.

## 128 KiB ESP32-C6 Profile

The optional physical-board profile reserves 128 KiB of executable cartridge
RAM and uses the ILI9341 low-memory renderer: the persistent framebuffer is
the 320x200 game viewport, while the two 20-pixel status bands are generated
during LCD transfer. This keeps the game coordinate system unchanged. Full
320x240 framebuffer drawing is therefore not available in this profile.

```bash
idf.py -B build-esp32c6-128k \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6_128k" \
  set-target esp32c6
idf.py -B build-esp32c6-128k build
```

Then build large cartridges with `--cart-ram-kib 128` (see below). Board
uploads check the limit against the firmware's `/api/runtime` response. The
setup resource screen reports `CART RAM` so the active profile can be checked
on the board.

## Matching Host Tools to the Profile

Host tools reject a cartridge whose executable memory (`mem_size`: code, data,
and `.bss`) is larger than the target's cartridge RAM window. Portable
`cartridge build` runs and QEMU staging cannot ask a running firmware for
its window, so they choose the limit as follows:

| Command | Limit used |
|---|---|
| `python3 -m prg32 cartridge build` | `--cart-ram-kib`, else 64 KiB (the default extended profile) |
| `python3 -m prg32 qemu upload` | `--cart-ram-kib`, else `CONFIG_PRG32_CART_RAM_KIB` from the `sdkconfig` next to `--flash` (normally `build-qemu/sdkconfig`), else 64 KiB |
| `python3 -m prg32 esp32c6 upload` | `cart_ram_size` reported by the board's `/api/runtime` |

`--cart-ram-kib` accepts 16 to 128, the same range as the Kconfig custom
profile. The value in `prg32/utilities/env_variables.py`
(`DEFAULT_CART_RAM_KIB`) must match the Kconfig `PRG32_CART_RAM_KIB` default.

To keep a cartridge within the classroom profile, build it with the
32 KiB limit:

```bash
python3 -m prg32 cartridge build game.S --out build/game.prg32 \
  --name game --entry-prefix game --cart-ram-kib 32
```

For a QEMU firmware built with the classroom profile, `qemu upload` picks up
32 KiB from `build-qemu/sdkconfig`; pass `--cart-ram-kib 32` when staging into
a flash image with no `sdkconfig` next to it.
