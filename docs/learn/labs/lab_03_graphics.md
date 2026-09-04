# Lab 03 - Graphics and Frame Updates

## Goal

Draw a moving rectangle in the 320x200 game viewport.

## Steps

1. Clear the frame:

```asm
li a0, 0
call prg32_gfx_clear
```

2. Load `x` and `y` from `.data`.
3. Draw a rectangle:

```asm
mv a0, t0
mv a1, t1
li a2, 24
li a3, 12
li a4, 65535
call prg32_gfx_rect
```

4. Update `x` by a signed velocity each frame.
5. Reverse velocity at the screen edges.
6. Call `prg32_gfx_present` from the main loop or rely on the base app loop.

## Checkpoint

The rectangle bounces horizontally without leaving old pixels behind.

You may show this checkpoint on either the physical ILI9341 display or the QEMU
virtual screen.

For QEMU:

```bash
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

Optional extension: build your rectangle game as a `.prg32` cartridge and upload
it without reflashing the resident firmware. Use `docs/cartridges.md`.

Feature extension: run one demo under `examples/features` and identify which
helper implements scrolling, animation, or dual playfields.

Memory extension: convert one sprite in RGB565 and 4-bpp indexed modes, record
the generated pixel-data sizes, and draw both. Then run `PERFORMANCE TEST` from
setup mode and explain the paired `RGB FPS` and `IDX FPS` columns. Download
`/api/performance.json` and locate the matching entries in `comparisons`.

Performance extension: repeat the paired benchmark with one fixed firmware
revision and optimization profile. Treat draw time as CPU composition work and
present time as RGB565 display-transfer work. The current RGB565 and indexed
renderers use equivalent one-lock, clipped-row, one-dirty-update policies.

## Reflection

Explain why clearing every frame is simple but not always efficient. Why does
indexed asset storage save cartridge memory without reducing the RGB565 display
transfer size?
