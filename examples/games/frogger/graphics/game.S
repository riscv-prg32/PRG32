/*
 * FROGGER GRAPHICS for PRG32.
 *
 * Minimal assembly demo for the 24x24 multicolor sprite helper.
 */
.option norelax

.section .data
frogger_graphics_x: .word 148
frogger_graphics_y: .word 168
frogger_graphics_car_x: .word 0
frogger_graphics_title: .asciz "24X24 SPRITE"

.section .rodata
.balign 2
frogger_graphics_sprite:
    .rept 3
    .half 0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff
    .endr
    .rept 4
    .half 0xffff,0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff,0xffff
    .endr
    .rept 4
    .half 0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff
    .endr
    .rept 4
    .half 0xffff,0x07e0,0x07e0,0x07e0,0x0000,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x0000,0x07e0,0x07e0,0x07e0,0xffff
    .endr
    .rept 4
    .half 0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff
    .endr
    .rept 5
    .half 0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff
    .endr

.section .text
.global frogger_graphics_init
.global frogger_graphics_update
.global frogger_graphics_draw

frogger_graphics_init:
    la t0, frogger_graphics_x
    li t1, 148
    sw t1, 0(t0)
    la t0, frogger_graphics_y
    li t1, 168
    sw t1, 0(t0)
    la t0, frogger_graphics_car_x
    sw zero, 0(t0)
    ret

frogger_graphics_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t2, a0
    la t0, frogger_graphics_x
    lw t1, 0(t0)
    andi t3, t2, 1
    beqz t3, .Lfrogger_graphics_no_left
    addi t1, t1, -24
.Lfrogger_graphics_no_left:
    andi t3, t2, 2
    beqz t3, .Lfrogger_graphics_no_right
    addi t1, t1, 24
.Lfrogger_graphics_no_right:
    blt t1, zero, .Lfrogger_graphics_min_x
    li t3, 296
    bgt t1, t3, .Lfrogger_graphics_max_x
    j .Lfrogger_graphics_store_x
.Lfrogger_graphics_min_x:
    li t1, 0
    j .Lfrogger_graphics_store_x
.Lfrogger_graphics_max_x:
    li t1, 296
.Lfrogger_graphics_store_x:
    sw t1, 0(t0)

    la t0, frogger_graphics_y
    lw t1, 0(t0)
    andi t3, t2, 4
    beqz t3, .Lfrogger_graphics_no_up
    addi t1, t1, -24
.Lfrogger_graphics_no_up:
    andi t3, t2, 8
    beqz t3, .Lfrogger_graphics_no_down
    addi t1, t1, 24
.Lfrogger_graphics_no_down:
    blt t1, zero, .Lfrogger_graphics_min_y
    li t3, 176
    bgt t1, t3, .Lfrogger_graphics_max_y
    j .Lfrogger_graphics_store_y
.Lfrogger_graphics_min_y:
    li t1, 0
    j .Lfrogger_graphics_store_y
.Lfrogger_graphics_max_y:
    li t1, 176
.Lfrogger_graphics_store_y:
    sw t1, 0(t0)

    la t0, frogger_graphics_car_x
    lw t1, 0(t0)
    addi t1, t1, 4
    li t2, 320
    blt t1, t2, .Lfrogger_graphics_car_ok
    li t1, -48
.Lfrogger_graphics_car_ok:
    sw t1, 0(t0)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

frogger_graphics_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    li a0, 0x03ef
    call prg32_gfx_clear
    li a0, 0
    li a1, 56
    li a2, 320
    li a3, 88
    li a4, 0x4208
    call prg32_gfx_rect
    la t0, frogger_graphics_car_x
    lw a0, 0(t0)
    li a1, 96
    li a2, 48
    li a3, 18
    li a4, 0xf800
    call prg32_gfx_rect
    la t0, frogger_graphics_x
    lw a0, 0(t0)
    la t0, frogger_graphics_y
    lw a1, 0(t0)
    la a2, frogger_graphics_sprite
    call prg32_sprite_draw_24x24
    li a0, 8
    li a1, 8
    la a2, frogger_graphics_title
    li a3, 0xffff
    li a4, 0x03ef
    call prg32_gfx_text8

    lw ra, 12(sp)
    addi sp, sp, 16
    ret
