# Tetris-Inspired Example

This directory contains RISC-V assembly and C versions of a falling-block PRG32
demo. It is intentionally small and classroom-friendly: one T-shaped piece
falls, can move left/right, can rotate, and resets when it reaches the bottom.

## Files

- `ascii/game.S`: text/debug version that prints position, rotation, and score.
- `graphics/game.S`: 320x200 graphics version with a 10x20 board and a falling
  T piece.
- `c/game.c`: C programming version with the same board and piece state.

## Controls

- LEFT / RIGHT: move the falling piece.
- DOWN: soft drop.
- A: rotate the piece.

## Learning Goals

- Keep game state in `.data`.
- Read the PRG32 input bitmask.
- Clamp a coordinate to a board.
- Use a frame counter to slow falling motion.
- Draw a tetromino from four repeated cells.
- Compare ASCII/debug output, graphics assembly, and C versions.

## Suggested Exercise

1. Run the ASCII version first and watch `x`, `y`, `rotation`, and `score`.
2. Run the graphics version and match those variables to what appears on screen.
3. Change the fall speed threshold.
4. Break the clamp limit so the piece leaves the board.
5. Fix the clamp and explain which branch protects the board boundary.
