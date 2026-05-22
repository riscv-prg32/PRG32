/*
 * RAYCASTER GRAPHICS for PRG32.
 *
 * A compact RISC-V graphics demake of a Doom-style corridor. The full playable
 * raycaster lives in c/game.c; this assembly version keeps the control flow
 * small enough for register tracing.
 */
.option norelax

.equ BTN_LEFT,  1
.equ BTN_RIGHT, 2
.equ BTN_UP,    4
.equ BTN_DOWN,  8

.section .data
raycaster_graphics_angle: .word 0
raycaster_graphics_depth: .word 12

.section .rodata
raycaster_graphics_title: .asciz "RAYCASTER ASM"
raycaster_graphics_help:  .asciz "TURN L/R  DEPTH U/D"

.section .text
.global raycaster_graphics_init
.global raycaster_graphics_update
.global raycaster_graphics_draw

raycaster_graphics_init:
    la t0, raycaster_graphics_angle
    sw zero, 0(t0)
    la t0, raycaster_graphics_depth
    li t1, 12
    sw t1, 0(t0)
    ret

raycaster_graphics_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t0, a0

    la t1, raycaster_graphics_angle
    lw t2, 0(t1)
    andi t3, t0, BTN_LEFT
    beqz t3, .Lraycaster_graphics_no_left
    addi t2, t2, -1
.Lraycaster_graphics_no_left:
    andi t3, t0, BTN_RIGHT
    beqz t3, .Lraycaster_graphics_no_right
    addi t2, t2, 1
.Lraycaster_graphics_no_right:
    andi t2, t2, 31
    sw t2, 0(t1)

    la t1, raycaster_graphics_depth
    lw t2, 0(t1)
    andi t3, t0, BTN_UP
    beqz t3, .Lraycaster_graphics_no_up
    addi t2, t2, 2
.Lraycaster_graphics_no_up:
    andi t3, t0, BTN_DOWN
    beqz t3, .Lraycaster_graphics_no_down
    addi t2, t2, -2
.Lraycaster_graphics_no_down:
    li t3, 0
    bge t2, t3, .Lraycaster_graphics_depth_min_ok
    mv t2, t3
.Lraycaster_graphics_depth_min_ok:
    li t3, 40
    ble t2, t3, .Lraycaster_graphics_depth_max_ok
    mv t2, t3
.Lraycaster_graphics_depth_max_ok:
    sw t2, 0(t1)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

raycaster_graphics_draw:
    addi sp, sp, -32
    sw ra, 28(sp)
    sw s0, 24(sp)
    sw s1, 20(sp)
    sw s2, 16(sp)
    sw s3, 12(sp)
    sw s4, 8(sp)

    li a0, 0
    li a1, 0
    li a2, 320
    li a3, 100
    li a4, 0x18e3
    call prg32_gfx_rect

    li a0, 0
    li a1, 100
    li a2, 320
    li a3, 100
    li a4, 0x4208
    call prg32_gfx_rect

    la t0, raycaster_graphics_angle
    lw s1, 0(t0)
    la t0, raycaster_graphics_depth
    lw s2, 0(t0)
    li s0, 0
.Lraycaster_graphics_strip_loop:
    add s3, s0, s1
    andi s3, s3, 7
    li t0, 10
    mul s3, s3, t0
    addi s3, s3, 34
    add s3, s3, s2

    li t0, 20
    mul s4, s0, t0

    li a0, 100
    srli t1, s3, 1
    sub a1, a0, t1
    mv a0, s4
    li a2, 20
    mv a3, s3
    andi t2, s0, 1
    beqz t2, .Lraycaster_graphics_cyan
    li a4, 0xf81f
    j .Lraycaster_graphics_color_ready
.Lraycaster_graphics_cyan:
    li a4, 0x07ff
.Lraycaster_graphics_color_ready:
    call prg32_gfx_rect

    mv a0, s4
    li t1, 100
    srli t2, s3, 1
    sub a1, t1, t2
    li a2, 1
    mv a3, s3
    li a4, 0x2104
    call prg32_gfx_rect

    addi s0, s0, 1
    li t0, 16
    blt s0, t0, .Lraycaster_graphics_strip_loop

    li a0, 8
    li a1, 8
    la a2, raycaster_graphics_title
    li a3, 0xffff
    li a4, 0
    call prg32_gfx_text8

    li a0, 8
    li a1, 184
    la a2, raycaster_graphics_help
    li a3, 0xffe0
    li a4, 0
    call prg32_gfx_text8

    lw s4, 8(sp)
    lw s3, 12(sp)
    lw s2, 16(sp)
    lw s1, 20(sp)
    lw s0, 24(sp)
    lw ra, 28(sp)
    addi sp, sp, 32
    ret
