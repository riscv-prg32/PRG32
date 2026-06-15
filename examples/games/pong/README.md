# Demo game

This directory contains RISC-V assembly and C versions of the game:

- `ascii/game.S`: uses the PRG32 console API and runs in UART-only, LCD-only text, or mirrored mode.
- `graphics/game.S`: uses the 320x200 graphics API tuned for ILI9341.
- `c/game.c`: uses the same PRG32 API from C and keeps the upper paddle moving
  with a simple fallback AI.

Build one version by adding the corresponding source file to
`main/CMakeLists.txt`, or package it as a `.prg32` cartridge with
`python3 -m prg32`.

In QEMU, use the monitor terminal keyboard mapping described in `docs/qemu.md`.
The C version keeps the upper paddle active with a simple fallback AI, so QEMU
still shows the two-paddle layout with one local joystick.

The assembly source is deliberately commented line-by-line for architecture
classes. The C source is intentionally direct for programming labs.
