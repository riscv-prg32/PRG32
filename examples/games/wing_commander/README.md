# Wing Commander-Inspired Example

This example demonstrates PRG32 dual playfields with a space-cockpit game. The
background playfield is a scrolling starfield outside the window; the foreground
playfield is a fixed cockpit/dashboard overlay. Sprites and HUD text are drawn
on top.

## Files

- `ascii/game.S`: state-tracing assembly version for crosshair coordinates.
- `graphics/game.S`: compact assembly dual-playfield cockpit.
- `c/game.c`: playable C version with enemies, laser fire, score, and shield.

## Controls

- LEFT / RIGHT / UP / DOWN: move the crosshair.
- A or B: fire the laser in the C version.

## Learning Goals

- Use `prg32_playfield_draw_dual()` for a background and foreground layer.
- Keep one playfield scrolling while the cockpit playfield remains fixed.
- Combine tile playfields, direct drawing, simple audio feedback, and HUD text.
- Compare compact RISC-V assembly with a fuller C game loop.

## Cartridge Prefixes

- ASCII: `wing_commander_ascii`
- Graphics assembly: `wing_commander_graphics`
- C: `wing_commander_c`
