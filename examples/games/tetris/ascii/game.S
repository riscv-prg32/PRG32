/*
 * TETRIS-INSPIRED ASCII for PRG32.
 *
 * This is a text/debug version of the falling-block demo. It does not draw a
 * full board. Instead, it prints the state variables that the graphics version
 * later turns into a visible piece: x column, y row, rotation, and score.
 */
.option norelax

.section .data
tetris_ascii_x:          .word 4
tetris_ascii_y:          .word 0
tetris_ascii_rotation:   .word 0
tetris_ascii_fall_count: .word 0
tetris_ascii_score:      .word 0

.section .rodata
tetris_ascii_title:      .asciz "TETRIS ASCII\n"
tetris_ascii_help:       .asciz "LEFT RIGHT DOWN A\n"
tetris_ascii_score_msg:  .asciz "SCORE "
tetris_ascii_x_msg:      .asciz " X "
tetris_ascii_y_msg:      .asciz " Y "
tetris_ascii_rot_msg:    .asciz " ROT "
tetris_ascii_shape0:     .asciz "###\n #\n"
tetris_ascii_shape1:     .asciz " #\n##\n #\n"
tetris_ascii_shape2:     .asciz " #\n###\n"
tetris_ascii_shape3:     .asciz "#\n##\n#\n"

.section .text
.global tetris_ascii_init
.global tetris_ascii_update
.global tetris_ascii_draw

tetris_ascii_init:
    addi sp, sp, -16
    sw ra, 12(sp)

    la t0, tetris_ascii_x
    li t1, 4
    sw t1, 0(t0)
    la t0, tetris_ascii_y
    sw zero, 0(t0)
    la t0, tetris_ascii_rotation
    sw zero, 0(t0)
    la t0, tetris_ascii_fall_count
    sw zero, 0(t0)
    la t0, tetris_ascii_score
    sw zero, 0(t0)

    call prg32_console_clear
    la a0, tetris_ascii_title
    call prg32_console_write
    la a0, tetris_ascii_help
    call prg32_console_write
    li a0, 180
    li a1, 40
    call prg32_audio_beep

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

tetris_ascii_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t0, a0

    /* Horizontal movement uses a 10-column board and clamps x to [0, 7]. */
    la t1, tetris_ascii_x
    lw t2, 0(t1)
    andi t3, t0, 1
    beqz t3, .Ltetris_ascii_check_right
    addi t2, t2, -1
.Ltetris_ascii_check_right:
    andi t3, t0, 2
    beqz t3, .Ltetris_ascii_clamp_x
    addi t2, t2, 1
.Ltetris_ascii_clamp_x:
    bgez t2, .Ltetris_ascii_clamp_x_high
    li t2, 0
.Ltetris_ascii_clamp_x_high:
    li t3, 7
    ble t2, t3, .Ltetris_ascii_store_x
    mv t2, t3
.Ltetris_ascii_store_x:
    sw t2, 0(t1)

    /* A rotates the piece. Holding A rotates repeatedly, which is easy to see. */
    andi t3, t0, 16
    beqz t3, .Ltetris_ascii_fall
    la t1, tetris_ascii_rotation
    lw t2, 0(t1)
    addi t2, t2, 1
    andi t2, t2, 3
    sw t2, 0(t1)

.Ltetris_ascii_fall:
    la t1, tetris_ascii_fall_count
    lw t2, 0(t1)
    addi t2, t2, 1

    andi t3, t0, 8
    bnez t3, .Ltetris_ascii_drop_now
    li t3, 8
    blt t2, t3, .Ltetris_ascii_store_fall_count

.Ltetris_ascii_drop_now:
    sw zero, 0(t1)
    la t1, tetris_ascii_y
    lw t2, 0(t1)
    addi t2, t2, 1
    li t3, 17
    ble t2, t3, .Ltetris_ascii_store_y
    li t2, 0
    sw t2, 0(t1)
    la t4, tetris_ascii_score
    lw t5, 0(t4)
    addi t5, t5, 1
    sw t5, 0(t4)
    li a0, 330
    li a1, 40
    call prg32_audio_beep
    j .Ltetris_ascii_done
.Ltetris_ascii_store_y:
    sw t2, 0(t1)
    j .Ltetris_ascii_done

.Ltetris_ascii_store_fall_count:
    sw t2, 0(t1)

.Ltetris_ascii_done:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

tetris_ascii_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_console_clear
    la a0, tetris_ascii_title
    call prg32_console_write
    la a0, tetris_ascii_help
    call prg32_console_write

    la a0, tetris_ascii_score_msg
    call prg32_console_write
    la t0, tetris_ascii_score
    lw a0, 0(t0)
    call prg32_console_hex32

    la a0, tetris_ascii_x_msg
    call prg32_console_write
    la t0, tetris_ascii_x
    lw a0, 0(t0)
    call prg32_console_hex32

    la a0, tetris_ascii_y_msg
    call prg32_console_write
    la t0, tetris_ascii_y
    lw a0, 0(t0)
    call prg32_console_hex32

    la a0, tetris_ascii_rot_msg
    call prg32_console_write
    la t0, tetris_ascii_rotation
    lw a0, 0(t0)
    call prg32_console_hex32

    li a0, 10
    call prg32_console_putc

    la t0, tetris_ascii_rotation
    lw t1, 0(t0)
    beqz t1, .Ltetris_ascii_shape0
    li t2, 1
    beq t1, t2, .Ltetris_ascii_shape1
    li t2, 2
    beq t1, t2, .Ltetris_ascii_shape2
    la a0, tetris_ascii_shape3
    j .Ltetris_ascii_print_shape
.Ltetris_ascii_shape0:
    la a0, tetris_ascii_shape0
    j .Ltetris_ascii_print_shape
.Ltetris_ascii_shape1:
    la a0, tetris_ascii_shape1
    j .Ltetris_ascii_print_shape
.Ltetris_ascii_shape2:
    la a0, tetris_ascii_shape2
.Ltetris_ascii_print_shape:
    call prg32_console_write

    lw ra, 12(sp)
    addi sp, sp, 16
    ret
