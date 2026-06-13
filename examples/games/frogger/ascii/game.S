/*
 * FROGGER ASCII for PRG32.
 *
 * A tiny grid-crossing exercise. It keeps the state readable for register
 * tracing before students move to the sprite versions.
 */
.option norelax

.section .data
frogger_ascii_x: .word 19
frogger_ascii_y: .word 10
frogger_ascii_car: .word 0
frogger_ascii_title: .asciz "FROGGER ASCII"
frogger_ascii_win: .asciz "SAFE"

.section .text
.global frogger_ascii_init
.global frogger_ascii_update
.global frogger_ascii_draw

frogger_ascii_init:
    la t0, frogger_ascii_x
    li t1, 19
    sw t1, 0(t0)
    la t0, frogger_ascii_y
    li t1, 10
    sw t1, 0(t0)
    la t0, frogger_ascii_car
    sw zero, 0(t0)
    ret

frogger_ascii_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t2, a0

    la t0, frogger_ascii_x
    lw t1, 0(t0)
    andi t3, t2, 1
    beqz t3, .Lfrogger_ascii_no_left
    addi t1, t1, -1
.Lfrogger_ascii_no_left:
    andi t3, t2, 2
    beqz t3, .Lfrogger_ascii_no_right
    addi t1, t1, 1
.Lfrogger_ascii_no_right:
    bltz t1, .Lfrogger_ascii_x_low
    li t3, 39
    bgt t1, t3, .Lfrogger_ascii_x_high
    j .Lfrogger_ascii_store_x
.Lfrogger_ascii_x_low:
    li t1, 0
    j .Lfrogger_ascii_store_x
.Lfrogger_ascii_x_high:
    li t1, 39
.Lfrogger_ascii_store_x:
    sw t1, 0(t0)

    la t0, frogger_ascii_y
    lw t1, 0(t0)
    andi t3, t2, 4
    beqz t3, .Lfrogger_ascii_no_up
    addi t1, t1, -1
.Lfrogger_ascii_no_up:
    andi t3, t2, 8
    beqz t3, .Lfrogger_ascii_no_down
    addi t1, t1, 1
.Lfrogger_ascii_no_down:
    bltz t1, .Lfrogger_ascii_y_low
    li t3, 23
    bgt t1, t3, .Lfrogger_ascii_y_high
    j .Lfrogger_ascii_store_y
.Lfrogger_ascii_y_low:
    li t1, 0
    j .Lfrogger_ascii_store_y
.Lfrogger_ascii_y_high:
    li t1, 23
.Lfrogger_ascii_store_y:
    sw t1, 0(t0)

    la t0, frogger_ascii_car
    lw t1, 0(t0)
    addi t1, t1, 1
    li t2, 40
    blt t1, t2, .Lfrogger_ascii_car_ok
    li t1, 0
.Lfrogger_ascii_car_ok:
    sw t1, 0(t0)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

frogger_ascii_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_console_clear
    la a0, frogger_ascii_title
    call prg32_console_write
    li a0, 10
    call prg32_console_putc

    la t0, frogger_ascii_y
    lw t1, 0(t0)
    bnez t1, .Lfrogger_ascii_no_win
    la a0, frogger_ascii_win
    call prg32_console_write
    li a0, 10
    call prg32_console_putc
.Lfrogger_ascii_no_win:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
