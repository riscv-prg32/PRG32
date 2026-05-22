/*
 * WING COMMANDER ASCII for PRG32.
 *
 * Text-state version for tracing input, crosshair coordinates, and frame
 * counters before moving to the graphics dual-playfield cockpit.
 */
.option norelax

.equ BTN_LEFT,  1
.equ BTN_RIGHT, 2
.equ BTN_UP,    4
.equ BTN_DOWN,  8

.section .data
wing_commander_ascii_x:     .word 160
wing_commander_ascii_y:     .word 94
wing_commander_ascii_frame: .word 0

.section .rodata
wing_commander_ascii_title: .asciz "WING COMMANDER ASCII\n"
wing_commander_ascii_help:  .asciz "MOVE CROSSHAIR WITH JOYSTICK\n"
wing_commander_ascii_x_msg: .asciz " X "
wing_commander_ascii_y_msg: .asciz " Y "
wing_commander_ascii_f_msg: .asciz " FRAME "

.section .text
.global wing_commander_ascii_init
.global wing_commander_ascii_update
.global wing_commander_ascii_draw

wing_commander_ascii_init:
    la t0, wing_commander_ascii_x
    li t1, 160
    sw t1, 0(t0)
    la t0, wing_commander_ascii_y
    li t1, 94
    sw t1, 0(t0)
    la t0, wing_commander_ascii_frame
    sw zero, 0(t0)
    ret

wing_commander_ascii_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t0, a0

    la t1, wing_commander_ascii_x
    lw t2, 0(t1)
    andi t3, t0, BTN_LEFT
    beqz t3, .Lwing_ascii_no_left
    addi t2, t2, -4
.Lwing_ascii_no_left:
    andi t3, t0, BTN_RIGHT
    beqz t3, .Lwing_ascii_no_right
    addi t2, t2, 4
.Lwing_ascii_no_right:
    sw t2, 0(t1)

    la t1, wing_commander_ascii_y
    lw t2, 0(t1)
    andi t3, t0, BTN_UP
    beqz t3, .Lwing_ascii_no_up
    addi t2, t2, -3
.Lwing_ascii_no_up:
    andi t3, t0, BTN_DOWN
    beqz t3, .Lwing_ascii_no_down
    addi t2, t2, 3
.Lwing_ascii_no_down:
    sw t2, 0(t1)

    la t1, wing_commander_ascii_frame
    lw t2, 0(t1)
    addi t2, t2, 1
    sw t2, 0(t1)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

wing_commander_ascii_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_console_clear
    la a0, wing_commander_ascii_title
    call prg32_console_write
    la a0, wing_commander_ascii_help
    call prg32_console_write

    la a0, wing_commander_ascii_x_msg
    call prg32_console_write
    la t0, wing_commander_ascii_x
    lw a0, 0(t0)
    call prg32_console_hex32

    la a0, wing_commander_ascii_y_msg
    call prg32_console_write
    la t0, wing_commander_ascii_y
    lw a0, 0(t0)
    call prg32_console_hex32

    la a0, wing_commander_ascii_f_msg
    call prg32_console_write
    la t0, wing_commander_ascii_frame
    lw a0, 0(t0)
    call prg32_console_hex32

    lw ra, 12(sp)
    addi sp, sp, 16
    ret
