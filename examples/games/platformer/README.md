# Platformer Example

This platform-game example is inspired by the
[Tilengine platformer sample](https://www.tilengine.org/samples.html), especially
its focus on scrolling, parallax/depth, and sprite animation. PRG32 keeps the
idea small enough for classroom tracing:

- layer 0: slow parallax sky
- layer 1: solid foreground platforms
- player actor: rectangle collision, jump, gravity, camera follow

## Files

- `ascii/game.S`: debug-oriented assembly version that prints actor state.
- `graphics/game.S`: assembly graphics version with parallax and a player box.
- `c/game.c`: C programming version using the same PRG32 platform helpers.

## Controls

- LEFT / RIGHT: walk.
- A or UP: jump.

## Learning Goals

- Define solid/platform/hazard tile flags.
- Build a small tile world.
- Step an actor with gravity and tile collision.
- Follow an actor with a camera.
- Compare RISC-V assembly and C implementations of the same idea.
