# Renderer architecture

Poing is a software-rendering workload built only on PRG32's stable public calls. `update` reads the controller and advances a small deterministic state; `draw` constructs the entire 320×200 scene.

1. Clear the game viewport and place deterministic twinkling stars.
2. Project an animated floor grid toward a movable horizon.
3. Rasterize a flattened ellipse as a contact shadow.
4. For each two-line sphere band, solve its X extent and Z depth using an integer square root.
5. Derive perspective texture coordinates from `(x, y, z)`, rotate them, sample a checker field plus an original 3×5 `PRG32` wordmark, then apply directional lighting.
6. Quantize lighting into stable 8-bit system-palette indices and coalesce
   adjacent equal indices into two-pixel-high rectangles, minimizing ABI calls
   without using private framebuffer pointers.
7. Overlay the optional control HUD. The resident runtime serializes the
   cartridge draw callback, so the cartridge needs no non-portable graphics
   locking calls.

The bounce phase wrap triggers a short low MIDI note through the portable
audio API. This keeps the sound synchronized with the procedural animation and
requires no embedded PCM asset.

The scene is not a sprite animation. Camera changes affect geometry and texture projection immediately, including while animation is frozen.

## Performance mode

SELECT starts a 300-frame `poing-indexed-renderer` suite through the public
performance broker. The single `poing-gameplay` case records update, draw, and
explicit present intervals with `PRG32_PERF_COLOR_INDEXED`. The normal scene,
input handling, procedural audio, and renderer stay active, making this an
application-level alternative to the synthetic reference performance cartridge.
SELECT aborts an active run safely. Completed hardware results are available at
`/api/performance.json`; QEMU validates execution but is not a hardware result.

## Load profile

The largest zoom evaluates roughly 3,000 sphere samples per frame. Each
requires an integer square root, perspective divisions, procedural texture
sampling, and light quantization. Identically colored neighbors are emitted as
spans, keeping the cartridge responsive in QEMU while retaining a meaningful
CPU workload and the same instructional rendering pipeline.

The cartridge itself performs no dynamic allocation; the resident broker uses
its documented temporary sample workspace while a performance run is active.
Code and constants remain inside the 128 KiB cartridge image and 32 KiB
cartridge RAM defaults documented by the target branch.
