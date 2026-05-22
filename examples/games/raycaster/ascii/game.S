/*
 * RAYCASTER ASCII for PRG32.
 *
 * Text-mode state trace for the raycaster lesson. Students can watch position
 * and angle variables change before moving to the graphics and C versions.
 */
.option norelax

.equ BTN_LEFT,  1
.equ BTN_RIGHT, 2
.equ BTN_UP,    4
.equ BTN_DOWN,  8

.section .data
raycaster_ascii_x:     .word 0x000000a0
raycaster_ascii_y:     .word 0x00000220
raycaster_ascii_angle: .word 30

.section .rodata
raycaster_ascii_title: .asciz "RAYCASTER ASCII\n"
raycaster_ascii_help:  .asciz "LEFT/RIGHT TURN  UP/DOWN MOVE\n"
raycaster_ascii_x_msg: .asciz " X "
raycaster_ascii_y_msg: .asciz " Y "
raycaster_ascii_a_msg: .asciz " ANGLE "

.section .text
.global raycaster_ascii_init
.global raycaster_ascii_update
.global raycaster_ascii_draw

raycaster_ascii_init:
    la t0, raycaster_ascii_x
    li t1, 0x000000a0
    sw t1, 0(t0)
    la t0, raycaster_ascii_y
    li t1, 0x00000220
    sw t1, 0(t0)
    la t0, raycaster_ascii_angle
    li t1, 30
    sw t1, 0(t0)
    ret

raycaster_ascii_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t0, a0

    la t1, raycaster_ascii_angle
    lw t2, 0(t1)
    andi t3, t0, BTN_LEFT
    beqz t3, .Lraycaster_ascii_no_left
    addi t2, t2, -1
.Lraycaster_ascii_no_left:
    andi t3, t0, BTN_RIGHT
    beqz t3, .Lraycaster_ascii_no_right
    addi t2, t2, 1
.Lraycaster_ascii_no_right:
    andi t2, t2, 31
    sw t2, 0(t1)

    la t1, raycaster_ascii_y
    lw t2, 0(t1)
    andi t3, t0, BTN_UP
    beqz t3, .Lraycaster_ascii_no_up
    addi t2, t2, -8
.Lraycaster_ascii_no_up:
    andi t3, t0, BTN_DOWN
    beqz t3, .Lraycaster_ascii_no_down
    addi t2, t2, 8
.Lraycaster_ascii_no_down:
    sw t2, 0(t1)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

raycaster_ascii_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_console_clear
    la a0, raycaster_ascii_title
    call prg32_console_write
    la a0, raycaster_ascii_help
    call prg32_console_write

    la a0, raycaster_ascii_x_msg
    call prg32_console_write
    la t0, raycaster_ascii_x
    lw a0, 0(t0)
    call prg32_console_hex32

    la a0, raycaster_ascii_y_msg
    call prg32_console_write
    la t0, raycaster_ascii_y
    lw a0, 0(t0)
    call prg32_console_hex32

    la a0, raycaster_ascii_a_msg
    call prg32_console_write
    la t0, raycaster_ascii_angle
    lw a0, 0(t0)
    call prg32_console_hex32

    lw ra, 12(sp)
    addi sp, sp, 16
    ret
