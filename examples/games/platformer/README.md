# Platformer Example

This platform-game example is inspired by the structure of classic
side-scrolling platform games and by the
[Tilengine platformer sample](https://www.tilengine.org/samples.html). PRG32
keeps the idea small enough for classroom tracing:

- layer 0: slow parallax sky
- layer 1: solid foreground platforms, gaps, pipes, coins, hazards, and a goal
- player actor: rectangle collision, jump, gravity, camera follow
- C version: a playable mini-course using the tile-engine helpers

## Files

- `ascii/game.S`: debug-oriented assembly version that prints actor state.
- `graphics/game.S`: assembly graphics version with parallax and a player box.
- `c/game.c`: playable C version using the same PRG32 platform helpers.

## Controls

- LEFT / RIGHT: walk.
- A or UP: jump.
- B in the C version: reset the course.

## Learning Goals

- Define solid/platform/hazard tile flags.
- Build a small tile world.
- Step an actor with gravity and tile collision.
- Follow an actor with a camera.
- Remove collectible tiles when the actor overlaps them.
- Compare RISC-V assembly and C implementations of the same idea.
