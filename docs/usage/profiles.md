# PRG32 Profiles

PRG32 uses different RAM profiles to adjust how much internal executable RAM the resident firmware reserves for uploadable cartridges. The active profile dictates the cartridge size limits and the available drawing features.

## Default Profiles

- **Classroom Profile (32 KiB)**: The default for physical ESP32-C6 classroom boards. It leaves more SRAM available to the resident runtime, Wi-Fi, setup menus, diagnostics, and standard framebuffer work.
- **Extended Profile (64 KiB)**: The default used by QEMU emulator builds.

## 128 KiB ESP32-C6 Profile

The optional physical-board profile reserves 128 KiB of executable cartridge
RAM and uses the ILI9341 low-memory renderer: the persistent framebuffer is
the 320x200 game viewport, while the two 20-pixel status bands are generated
during LCD transfer. This keeps the game coordinate system unchanged. Full
320x240 framebuffer drawing is therefore not available in this profile.

```bash
idf.py -B build-esp32c6-128k \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6_128k" \
  set-target esp32c6
idf.py -B build-esp32c6-128k build
```

Then use that firmware ELF or its `/api/runtime` response when building and
uploading large cartridges. The setup resource screen reports `CART RAM` so
the active profile can be checked on the board.
