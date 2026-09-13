# Feature matrix

| Page | Demonstrates | development-c6 relevance |
|---|---|---|
| Overview | viewport, bands, diagnostics | baseline runtime |
| Input | P1/P2/controller | local multiplayer input |
| Primitives | pixel/rect/text | RGB565 baseline |
| Indexed | `prg32_sprite_draw_indexed` | new 1/2/4/8-bpp path |
| Bitplanes | `prg32_sprite_draw_bitplanes` | new bitplane path |
| Playfield | tiles, layers, parallax | game-world helpers |
| Platform | collision/gravity/camera | classroom game helper |
| Synth | procedural instruments + tracker | new SID-like synthesis |
| Audio | beep/tone/note | compatibility path |
| Diagnostics | frame/input debug overlay | portable diagnostics |
| Services | WiFi/score state | deployment integration |

The demo avoids firmware-private helpers and destructive service calls. It does
not change WiFi configuration, erase cartridges, publish scores, or create
multiplayer sessions by itself. Procedural instrument descriptors and tracker
events live in the cartridge AUD0 block rather than being registered through
non-portable firmware functions.
