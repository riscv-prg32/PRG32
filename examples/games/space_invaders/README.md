# Demo game

This directory contains RISC-V assembly and C versions of the game:

- `ascii/game.S`: uses the PRG32 console API and runs in UART-only, LCD-only text, or mirrored mode.
- `graphics/game.S`: uses the 320x200 graphics API tuned for ILI9341.
- `c/game.c`: uses the same PRG32 API from C.

Build one version by adding the corresponding source file to
`main/CMakeLists.txt`, or package it as a `.prg32` cartridge with
`tools/prg32_game.py`.

In QEMU, use the monitor terminal keyboard mapping described in `docs/qemu.md`.

The assembly source is deliberately commented line-by-line for architecture
classes. The C source is intentionally direct for programming labs.
