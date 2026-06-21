# PRG32 Bounce

`PRG32 Bounce` is a deliberately small demo game inspired by the motion and
bold RGB565 colours of early home-computer demo screens. Its artwork and PRG32
branding are original; it does not copy the Amiga Boing Ball artwork.

It first shows a 320x200 RGB565 game splash screen. Press **Start** to launch a
24x24 colour sprite that bounces around the same game viewport. The splash and
the sprite both carry the PRG32 name, making the transition easy to spot on an
ILI9341 board or in QEMU.

## Controls

- Start: leave the splash screen and begin the animation.

## Files

- `ascii/game.S`: console-first state-machine version for register tracing.
- `graphics/game.S`: assembly RGB565 sprite version.
- `c/game.c`: C version with an edge-triggered Start button and a hand-written
  24x24 RGB565 ball asset.

## Learning Goals

- Keep a title-state flag until `PRG32_BTN_START` is pressed.
- Use `prg32_splash_draw_game` for a viewport-safe 320x200 splash screen.
- Store a 24x24 colour sprite as `24 * 24` RGB565 halfwords.
- Reverse a velocity at the visible 320x200 viewport edges.
