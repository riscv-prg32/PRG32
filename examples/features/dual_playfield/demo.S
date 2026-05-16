/*
 * PRG32 dual playfield feature demo.
 *
 * Layer 0 is opaque. Layer 1 treats tile 0 as transparent, so platforms float
 * over the moving checker background.
 */
.option norelax

.section .data
dual_playfield_scroll: .word 0

.section .rodata
dual_playfield_title: .asciz "DUAL PLAYFIELD"
dual_tile_blank:      .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
dual_tile_checker:    .byte 0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55
dual_tile_platform:   .byte 0xff,0xff,0x81,0xbd,0xbd,0x81,0xff,0xff

.section .text
.global dual_playfield_init
.global dual_playfield_update
.global dual_playfield_draw

dual_playfield_init:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    li a0, 0
    la a1, dual_tile_blank
    li a2, 0
    li a3, 0
    call prg32_tile_define

    li a0, 1
    la a1, dual_tile_checker
    li a2, 0x001f
    li a3, 0
    call prg32_tile_define

    li a0, 2
    la a1, dual_tile_platform
    li a2, 0xffff
    li a3, 0xf800
    call prg32_tile_define

    li a0, 0
    li a1, 1
    call prg32_playfield_clear

    li a0, 1
    li a1, 0
    call prg32_playfield_clear

    li s0, 0
.Ldual_platform_loop:
    li a0, 1
    mv a1, s0
    li a2, 16
    li a3, 2
    call prg32_playfield_put

    li a0, 1
    mv a1, s0
    li a2, 17
    li a3, 2
    call prg32_playfield_put

    addi s0, s0, 2
    li t0, 64
    blt s0, t0, .Ldual_platform_loop

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

dual_playfield_update:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    call prg32_ticks_ms
    srli s0, a0, 5
    la t0, dual_playfield_scroll
    sw s0, 0(t0)

    li a0, 0
    srli a1, s0, 1
    li a2, 0
    call prg32_playfield_scroll

    li a0, 1
    mv a1, s0
    li a2, 0
    call prg32_playfield_scroll

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

dual_playfield_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    li a0, 0
    call prg32_gfx_clear
    call prg32_playfield_draw_dual

    li a0, 8
    li a1, 8
    la a2, dual_playfield_title
    li a3, 0xffff
    li a4, 0
    call prg32_gfx_text8

    lw ra, 12(sp)
    addi sp, sp, 16
    ret
