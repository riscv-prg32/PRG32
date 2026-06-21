/*
 * PRG32 BOUNCE graphics: a 320x200 splash followed by a 24x24 RGB565 ball.
 * The compact 24x24 asset uses a coloured square so students can replace it
 * with a hand-drawn logo of their own.
 */
.option norelax

.section .data
prg32_bounce_graphics_started: .word 0
prg32_bounce_graphics_x: .word 148
prg32_bounce_graphics_y: .word 88
prg32_bounce_graphics_dx: .word 2
prg32_bounce_graphics_dy: .word 2
prg32_bounce_graphics_title: .asciz "PRG32 BOUNCE"
prg32_bounce_graphics_start: .asciz "PRESS START"
prg32_bounce_graphics_logo: .asciz "PRG32"

.section .rodata
.balign 2
prg32_bounce_graphics_ball:
    /* 24 rows of a simple RGB565 red/white/blue logo ball. */
    .rept 2
    .half 0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011
    .endr
    .rept 4
    .half 0x0011,0x0011,0x0000,0xffff,0xffff,0xffff,0xffff,0xf800,0xf800,0x001f,0xffff,0xffff,0x001f,0xf800,0xf800,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x0000,0x0011,0x0011
    .endr
    .rept 12
    .half 0x0011,0x0000,0xffff,0xffff,0xffff,0xf800,0xf800,0xf800,0x001f,0xffff,0xffff,0xffff,0x001f,0xf800,0xf800,0xf800,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x0000,0x0011
    .endr
    .rept 4
    .half 0x0011,0x0011,0x0000,0xffff,0xffff,0xffff,0xffff,0xffff,0xf800,0xf800,0xffff,0xffff,0xf800,0xf800,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x0000,0x0011,0x0011
    .endr
    .rept 2
    .half 0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011,0x0011
    .endr

.section .text
.global prg32_bounce_graphics_init
.global prg32_bounce_graphics_update
.global prg32_bounce_graphics_draw

prg32_bounce_graphics_init:
    la t0, prg32_bounce_graphics_started
    sw zero, 0(t0)
    la t0, prg32_bounce_graphics_x
    li t1, 148
    sw t1, 0(t0)
    la t0, prg32_bounce_graphics_y
    li t1, 88
    sw t1, 0(t0)
    ret

prg32_bounce_graphics_update:
    addi sp, sp, -16
    sw ra, 12(sp)
    call prg32_input_read
    la t0, prg32_bounce_graphics_started
    lw t1, 0(t0)
    bnez t1, .Lprg32_bounce_graphics_move
    andi t2, a0, 64                 /* PRG32_BTN_START */
    beqz t2, .Lprg32_bounce_graphics_done
    li t1, 1
    sw t1, 0(t0)
    li a0, 660
    li a1, 60
    call prg32_audio_beep
    j .Lprg32_bounce_graphics_done
.Lprg32_bounce_graphics_move:
    la t0, prg32_bounce_graphics_x
    lw t1, 0(t0)
    la t2, prg32_bounce_graphics_dx
    lw t3, 0(t2)
    add t1, t1, t3
    bltz t1, .Lprg32_bounce_graphics_flip_x
    li t4, 296
    ble t1, t4, .Lprg32_bounce_graphics_store_x
.Lprg32_bounce_graphics_flip_x:
    sub t3, zero, t3
    sw t3, 0(t2)
    add t1, t1, t3
.Lprg32_bounce_graphics_store_x:
    sw t1, 0(t0)
    la t0, prg32_bounce_graphics_y
    lw t1, 0(t0)
    la t2, prg32_bounce_graphics_dy
    lw t3, 0(t2)
    add t1, t1, t3
    bltz t1, .Lprg32_bounce_graphics_flip_y
    li t4, 176
    ble t1, t4, .Lprg32_bounce_graphics_store_y
.Lprg32_bounce_graphics_flip_y:
    sub t3, zero, t3
    sw t3, 0(t2)
    add t1, t1, t3
.Lprg32_bounce_graphics_store_y:
    sw t1, 0(t0)
.Lprg32_bounce_graphics_done:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

prg32_bounce_graphics_draw:
    addi sp, sp, -16
    sw ra, 12(sp)
    la t0, prg32_bounce_graphics_started
    lw t1, 0(t0)
    bnez t1, .Lprg32_bounce_graphics_playing
    la a0, prg32_bounce_graphics_title
    la a1, prg32_bounce_graphics_start
    li a2, 0x0011
    li a3, 0xffff
    li a4, 0xf800
    call prg32_splash_draw_game
    j .Lprg32_bounce_graphics_finish
.Lprg32_bounce_graphics_playing:
    li a0, 0x0011
    call prg32_gfx_clear
    li a0, 8
    li a1, 8
    la a2, prg32_bounce_graphics_logo
    li a3, 0xffff
    li a4, 0x0011
    call prg32_gfx_text8
    la t0, prg32_bounce_graphics_x
    lw a0, 0(t0)
    la t0, prg32_bounce_graphics_y
    lw a1, 0(t0)
    la a2, prg32_bounce_graphics_ball
    call prg32_sprite_draw_24x24
.Lprg32_bounce_graphics_finish:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
