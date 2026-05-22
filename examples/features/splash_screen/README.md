# PRG32 Splash Screen Feature Demo

This demo shows the reusable 320x200 splash screen API for graphic games.

Learning goals:

- show a blocking game splash with `prg32_splash_show_game`
- draw a game splash state every frame with `prg32_splash_draw_game`
- call the same API from C and RISC-V assembly

## Sources

| Version | Source | Entry prefix |
|---|---|---|
| Assembly | `demo.S` | `splash_screen` |
| C | `c/demo.c` | `splash_screen_c` |

## Run Embedded In Firmware

Temporarily add the assembly source to `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "../examples/features/splash_screen/demo.S"
    REQUIRES prg32
    INCLUDE_DIRS "."
)
```

Call the symbols from `main/main.c`:

```c
void splash_screen_init(void);
void splash_screen_update(void);
void splash_screen_draw(void);
```

For the C version, use `../examples/features/splash_screen/c/demo.c` and the
`splash_screen_c_*` symbols.

Build:

```bash
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 -D SDKCONFIG=build-esp32c6/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build flash monitor
```

Checkpoint: the first splash pauses briefly, then the draw helper keeps a
320x200 title screen visible with a moving green bar. The top and bottom
physical-display bands remain outside the game viewport.

## Run As A Cartridge

```bash
python3 tools/prg32_game.py build \
  examples/features/splash_screen/demo.S \
  --firmware-elf build-esp32c6/PRG32.elf \
  --entry-prefix splash_screen \
  --name splash-asm \
  --out build-esp32c6/splash-asm.prg32
```

For the C version:

```bash
python3 tools/prg32_game.py build \
  examples/features/splash_screen/c/demo.c \
  --firmware-elf build-esp32c6/PRG32.elf \
  --entry-prefix splash_screen_c \
  --name splash-c \
  --out build-esp32c6/splash-c.prg32
```
