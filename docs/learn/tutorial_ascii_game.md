# Tutorial: Writing an ASCII Game in RISC-V Assembly

ASCII games are the fastest way to learn the PRG32 calling convention. They use
the same `init`, `update`, and `draw` structure as graphics games, but the output
is simple text.

## Learning Goals

- Export three game-specific entry points.
- Call C helpers from assembly.
- Store game state in `.data`.
- Read buttons as a bitmask.
- Preserve `ra` and stack alignment around calls.

## 1. Create the Game Skeleton

Create a new folder:

```text
examples/games/my_ascii_game/
|-- README.md
`-- ascii/game.S
```

Start `ascii/game.S` with:

```asm
.option norelax
.section .text

.global my_ascii_init
.global my_ascii_update
.global my_ascii_draw

my_ascii_init:
    addi sp, sp, -16
    sw ra, 12(sp)
    call prg32_console_clear
    la a0, title
    call prg32_console_write
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

my_ascii_update:
    ret

my_ascii_draw:
    ret

.section .rodata
title:
    .asciz "MY ASCII GAME\n"
```

Checkpoint: the file has exactly three exported symbols, and each symbol uses
your game prefix.

## 2. Add a Player Variable

Store position in memory:

```asm
.section .data
player_x:
    .word 10
```

Read and write the value in `update`:

```asm
my_ascii_update:
    la t0, player_x
    lw t1, 0(t0)
    addi t1, t1, 1
    sw t1, 0(t0)
    ret
```

Checkpoint: you can explain why `t0` holds an address and `t1` holds the value.

## 3. Read Buttons

Use `prg32_input_read` to test the left and right bits:

```asm
my_ascii_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t2, a0

    la t0, player_x
    lw t1, 0(t0)

    andi t3, t2, 1          # PRG32_BTN_LEFT
    beqz t3, .Lcheck_right
    addi t1, t1, -1

.Lcheck_right:
    andi t3, t2, 2          # PRG32_BTN_RIGHT
    beqz t3, .Lstore_player
    addi t1, t1, 1

.Lstore_player:
    sw t1, 0(t0)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
```

Debug question: why is the input mask copied from `a0` before more work happens?

## 4. Draw Text

Clear the console and print a simple frame:

```asm
my_ascii_draw:
    addi sp, sp, -16
    sw ra, 12(sp)
    call prg32_console_clear
    la a0, prompt
    call prg32_console_write
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

.section .rodata
prompt:
    .asciz "Move with LEFT and RIGHT\n"
```

First keep drawing simple. Once the input loop works, add number printing with
`prg32_console_hex32` or render position using repeated spaces.

## 5. Break and Fix Exercise

Break it:

1. Remove `sw ra, 12(sp)` before a `call`.
2. Build and run.
3. Observe whether the game returns correctly.

Fix it:

1. Restore the save/restore pair.
2. Rebuild.
3. Explain why `ra` changed during the call.

## Reflection

Answer these before moving to graphics:

- Which registers are used for arguments?
- Which registers did you use as temporary values?
- Which memory variable represents player state?
- Which instruction branches when a button is not pressed?
