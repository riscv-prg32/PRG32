# Poing — PRG32 real-time graphics showcase

Poing is an original, high-load graphics cartridge inspired by the technical spirit of the classic Amiga bouncing-ball demo. It uses no Amiga artwork: the sphere, wrapped **PRG32** block wordmark, lighting, shadow, star field, and perspective grid are generated from integer math every frame.

![Poing screenshot](assets/screenshot.png)

[Download the 30-second MP4 captured from the actual QEMU playfield and procedural impact audio](assets/preview.mp4).

## Controls

| Control | Effect |
|---|---|
| Joystick left/right | Orbit the viewpoint |
| Joystick up/down | Raise/lower the viewpoint |
| A | Cycle three camera distances / ball sizes |
| B | Freeze/resume time while retaining camera control |
| SELECT | Start or abort a 300-frame public-API performance run |

## What it stresses

- Per-scanline sphere intersection with integer square roots
- Perspective texture coordinates and rotating logo/checker texture
- Quantized 8-bit system-palette directional lighting
- Run-length coalescing into indexed `prg32_gfx_rect_indexed` spans
- Perspective floor, moving depth grid, shadow, stars, and HUD in the same frame
- A short I2S instrument note synchronized to each bounce
- Recursive graphics locking around the full composite
- Optional 300-frame performance-broker run using the normal Poing workload

The implementation deliberately avoids pre-rendered animation frames, floating point, heap allocation, and platform-private framebuffer access. It writes stable 8-bit indices through the public graphics ABI and configures the corresponding RGB565 palette once during initialization. Press SELECT to publish a normal Poing run through `/api/performance.json` as an alternative to the synthetic Performance Test cartridge. See `docs/architecture.md` for the rendering pipeline and performance tradeoffs.

## Build

From a checkout of PRG32's `main` branch, with its Python tooling and RISC-V toolchain available:

```sh
./build.sh /path/to/PRG32 esp32c6
./build.sh /path/to/PRG32 qemu
```

The portable, metadata-enriched cartridges are written to `dist/`. Upload with:

```sh
python3 -m prg32 esp32c6 upload dist/poing-esp32c6.prg32 --url http://192.168.4.1
```

Run the host validation without ESP-IDF:

```sh
./tests/run.sh
```

The root GitHub Actions workflow validates the renderer and media, builds both
architecture variants, and retains the packages and downloadable media in the
`poing-cartridge-package` artifact for 14 days.

## Compatibility

Targeted at PRG32 `main`, portable ABI 1.6, 320×200 centered game viewport. Entry prefix: `poing`.

## Originality

“Boing” is referenced only as historical inspiration for a real-time bouncing sphere demonstration. The code, palette, layout, procedural PRG32 wordmark, and generated catalog artwork in this project are original and licensed under MIT.
