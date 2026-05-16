# Demo game

This directory contains RISC-V assembly and C versions of the game:

- `ascii/game.S`: uses the PRG32 console API and runs in UART-only, LCD-only text, or mirrored mode.
- `graphics/game.S`: uses the 320x200 graphics API tuned for ILI9341.
- `c/game.c`: uses the same PRG32 API from C and supports optional player 2.

Build one version by adding the corresponding source file to
`main/CMakeLists.txt`, or package it as a `.prg32` cartridge with
`tools/prg32_game.py`.

In QEMU, input may be static unless a UART bridge is enabled.
The C version keeps player 2 active with a simple fallback paddle AI when no
second joystick input is present, so QEMU still shows the two-player layout.

The assembly source is deliberately commented line-by-line for architecture
classes. The C source is intentionally direct for programming labs.
