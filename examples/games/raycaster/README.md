# Raycaster Example

This Doom-inspired example demonstrates a simple first-person wall renderer on
the PRG32 RISC-V runtime. It is not a Doom port and does not use Doom assets;
it teaches the core idea behind early 2.5D shooters: cast one ray per screen
column, measure the wall distance, and draw a taller or shorter vertical slice.

## Files

- `ascii/game.S`: register-trace friendly state display for position and angle.
- `graphics/game.S`: compact assembly corridor renderer for graphics labs.
- `c/game.c`: full playable fixed-point raycaster.

## Controls

- LEFT / RIGHT: turn.
- UP / DOWN: move forward and backward.
- A / B in the C version: strafe left and right.

## Learning Goals

- Compare an assembly demake with a fuller C implementation.
- Use integer/fixed-point arithmetic instead of floating point.
- Understand how a 2.5D renderer converts distance into wall height.
- Explain why a small raycaster can run on the same RISC-V board used for
  assembly classes.

## Cartridge Prefixes

- ASCII: `raycaster_ascii`
- Graphics assembly: `raycaster_graphics`
- C: `raycaster_c`
