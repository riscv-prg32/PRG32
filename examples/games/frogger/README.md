# Frogger Inspired Crossing

This example uses 24x24 multicolor RGB565 sprites in a simple road-crossing
game. It is inspired by the arcade crossing pattern, but all names, graphics,
and rules are original classroom material.

## Controls

- Left and right: move one lane cell.
- Up and down: move between lanes.
- A: restart after a crash or a successful crossing.

## Files

- `ascii/game.S`: compact register-and-grid version for early assembly labs.
- `graphics/game.S`: assembly version that draws the 24x24 sprite helper.
- `c/game.c`: fuller sprite game with moving cars, score, and restart state.

## Learning Goals

- Store a 24x24 RGB565 sprite as `24 * 24` halfwords.
- Draw a multicolor sprite with `prg32_sprite_draw_24x24`.
- Use `prg32_sprite_hitbox` for rectangle collision.
- Keep movement snapped to visible lanes so game state is easy to inspect.
