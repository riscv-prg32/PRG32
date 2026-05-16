/*
 * TETRIS-INSPIRED GRAPHICS for PRG32.
 *
 * A tiny falling-block demo for the 320x200 PRG32 viewport. It draws one
 * T-shaped piece on a 10x20 board. This is not a full line-clearing game; it is
 * a compact example of input, frame timing, memory variables, and repeated
 * sprite-like drawing from assembly.
 */
#include "prg32_config.h"
.option norelax

.equ TETRIS_BOARD_X, 120
.equ TETRIS_BOARD_Y, 20
.equ TETRIS_CELL, 8

.section .data
tetris_graphics_x:          .word 4
tetris_graphics_y:          .word 0
tetris_graphics_rotation:   .word 0
tetris_graphics_fall_count: .word 0
tetris_graphics_score:      .word 0

.section .rodata
tetris_graphics_title:      .asciz "TETRIS"

.section .text
.global tetris_graphics_init
.global tetris_graphics_update
.global tetris_graphics_draw

tetris_graphics_init:
    addi sp, sp, -16
    sw ra, 12(sp)

    la t0, tetris_graphics_x
    li t1, 4
    sw t1, 0(t0)
    la t0, tetris_graphics_y
    sw zero, 0(t0)
    la t0, tetris_graphics_rotation
    sw zero, 0(t0)
    la t0, tetris_graphics_fall_count
    sw zero, 0(t0)
    la t0, tetris_graphics_score
    sw zero, 0(t0)

    call prg32_console_clear
    la a0, tetris_graphics_title
    call prg32_console_write
    li a0, 220
    li a1, 40
    call prg32_audio_beep

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

tetris_graphics_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t0, a0

    /* Move within a 10-column board. Max x is 7 for the widest T rotation. */
    la t1, tetris_graphics_x
    lw t2, 0(t1)
    andi t3, t0, 1
    beqz t3, .Ltetris_graphics_check_right
    addi t2, t2, -1
.Ltetris_graphics_check_right:
    andi t3, t0, 2
    beqz t3, .Ltetris_graphics_clamp_x
    addi t2, t2, 1
.Ltetris_graphics_clamp_x:
    bgez t2, .Ltetris_graphics_clamp_x_high
    li t2, 0
.Ltetris_graphics_clamp_x_high:
    li t3, 7
    ble t2, t3, .Ltetris_graphics_store_x
    mv t2, t3
.Ltetris_graphics_store_x:
    sw t2, 0(t1)

    /* Rotate on A. The low two bits are enough for four orientations. */
    andi t3, t0, 16
    beqz t3, .Ltetris_graphics_fall
    la t1, tetris_graphics_rotation
    lw t2, 0(t1)
    addi t2, t2, 1
    andi t2, t2, 3
    sw t2, 0(t1)

.Ltetris_graphics_fall:
    la t1, tetris_graphics_fall_count
    lw t2, 0(t1)
    addi t2, t2, 1

    andi t3, t0, 8
    bnez t3, .Ltetris_graphics_drop_now
    li t3, 8
    blt t2, t3, .Ltetris_graphics_store_fall_count

.Ltetris_graphics_drop_now:
    sw zero, 0(t1)
    la t1, tetris_graphics_y
    lw t2, 0(t1)
    addi t2, t2, 1
    li t3, 17
    ble t2, t3, .Ltetris_graphics_store_y
    li t2, 0
    sw t2, 0(t1)
    la t4, tetris_graphics_score
    lw t5, 0(t4)
    addi t5, t5, 1
    sw t5, 0(t4)
    li a0, 440
    li a1, 50
    call prg32_audio_beep
    j .Ltetris_graphics_done
.Ltetris_graphics_store_y:
    sw t2, 0(t1)
    j .Ltetris_graphics_done

.Ltetris_graphics_store_fall_count:
    sw t2, 0(t1)

.Ltetris_graphics_done:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

tetris_graphics_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    li a0, 0
    call prg32_gfx_clear

    /* Draw a blue board frame, then black interior. */
    li a0, 118
    li a1, 18
    li a2, 84
    li a3, 164
    li a4, 31
    call prg32_gfx_rect

    li a0, TETRIS_BOARD_X
    li a1, TETRIS_BOARD_Y
    li a2, 80
    li a3, 160
    li a4, 0
    call prg32_gfx_rect

    li a0, 8
    li a1, 8
    la a2, tetris_graphics_title
    li a3, 2047
    li a4, 0
    call prg32_gfx_text8

    la t0, tetris_graphics_rotation
    lw t1, 0(t0)
    beqz t1, .Ltetris_graphics_draw_rot0
    li t2, 1
    beq t1, t2, .Ltetris_graphics_draw_rot1
    li t2, 2
    beq t1, t2, .Ltetris_graphics_draw_rot2
    j .Ltetris_graphics_draw_rot3

.Ltetris_graphics_draw_rot0:
    call tetris_graphics_load_base
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 1
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 2
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 1
    addi a1, a1, 1
    call tetris_graphics_draw_cell
    j .Ltetris_graphics_draw_done

.Ltetris_graphics_draw_rot1:
    call tetris_graphics_load_base
    addi a0, a0, 1
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 1
    addi a1, a1, 1
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 1
    addi a1, a1, 2
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a1, a1, 1
    call tetris_graphics_draw_cell
    j .Ltetris_graphics_draw_done

.Ltetris_graphics_draw_rot2:
    call tetris_graphics_load_base
    addi a1, a1, 1
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 1
    addi a1, a1, 1
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 2
    addi a1, a1, 1
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 1
    call tetris_graphics_draw_cell
    j .Ltetris_graphics_draw_done

.Ltetris_graphics_draw_rot3:
    call tetris_graphics_load_base
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a1, a1, 1
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a1, a1, 2
    call tetris_graphics_draw_cell
    call tetris_graphics_load_base
    addi a0, a0, 1
    addi a1, a1, 1
    call tetris_graphics_draw_cell

.Ltetris_graphics_draw_done:
#if PRG32_DEBUG
    call prg32_input_read
    mv t1, a0
    la t0, tetris_graphics_score
    lw t2, 0(t0)
    li a0, 1
    la t0, tetris_graphics_x
    lw a1, 0(t0)
    la t0, tetris_graphics_y
    lw a2, 0(t0)
    mv a3, t1
    mv a4, t2
    call prg32_debug_overlay_draw
#endif

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

/*
 * Helper: load the current board column/row into a0/a1 and the piece color into
 * a4. It deliberately performs no C calls, so caller-saved temporaries are not
 * a concern.
 */
tetris_graphics_load_base:
    la t0, tetris_graphics_x
    lw a0, 0(t0)
    la t0, tetris_graphics_y
    lw a1, 0(t0)
    li a4, 65504
    ret

/*
 * Helper: draw one board cell.
 * Input:
 *   a0 = board column
 *   a1 = board row
 *   a4 = RGB565 color
 */
tetris_graphics_draw_cell:
    addi sp, sp, -16
    sw ra, 12(sp)
    slli a0, a0, 3
    addi a0, a0, TETRIS_BOARD_X
    slli a1, a1, 3
    addi a1, a1, TETRIS_BOARD_Y
    li a2, TETRIS_CELL
    li a3, TETRIS_CELL
    call prg32_gfx_rect
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
