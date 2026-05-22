/*
 * PRG32 splash screen feature demo.
 *
 * The init step shows a blocking 320x200 game splash. The draw step
 * demonstrates the non-blocking draw helper that a game can use in its own
 * title state.
 */
.option norelax

.section .data
splash_screen_frame: .word 0

.section .rodata
splash_screen_title:    .asciz "ASSEMBLY QUEST"
splash_screen_subtitle: .asciz "PRESS A TO BEGIN"
splash_screen_hint:     .asciz "SPLASH API FROM ASM"

.section .text
.global splash_screen_init
.global splash_screen_update
.global splash_screen_draw

splash_screen_init:
    addi sp, sp, -16
    sw ra, 12(sp)

    la a0, splash_screen_title
    la a1, splash_screen_subtitle
    li a2, 900
    li a3, 0x0000
    li a4, 0xffff
    li a5, 0x07ff
    call prg32_splash_show_game

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

splash_screen_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_ticks_ms
    srli a0, a0, 4
    la t0, splash_screen_frame
    sw a0, 0(t0)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

splash_screen_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    la a0, splash_screen_title
    la a1, splash_screen_hint
    li a2, 0x0000
    li a3, 0xffff
    li a4, 0xffe0
    call prg32_splash_draw_game

    la t0, splash_screen_frame
    lw t1, 0(t0)
    andi t1, t1, 127
    addi a0, t1, 96
    li a1, 154
    li a2, 32
    li a3, 6
    li a4, 0x07e0
    call prg32_gfx_rect

    lw ra, 12(sp)
    addi sp, sp, 16
    ret
