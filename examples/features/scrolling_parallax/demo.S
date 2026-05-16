/*
 * PRG32 scrolling/parallax feature demo.
 *
 * Layer 0 scrolls slowly like a far star field. Layer 1 scrolls at full camera
 * speed like foreground terrain. The camera is driven by time so QEMU shows
 * movement even without physical buttons.
 */
.option norelax

.section .data
scrolling_parallax_camera: .word 0

.section .rodata
scrolling_parallax_title: .asciz "SCROLL + PARALLAX"
scrolling_tile_blank:     .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
scrolling_tile_star:      .byte 0x00,0x10,0x00,0x44,0x00,0x10,0x00,0x00
scrolling_tile_ground:    .byte 0xff,0x81,0xbd,0xa5,0xbd,0x81,0xff,0xff
scrolling_tile_cloud:     .byte 0x00,0x38,0x7c,0xfe,0xfe,0x7c,0x38,0x00

.section .text
.global scrolling_parallax_init
.global scrolling_parallax_update
.global scrolling_parallax_draw

scrolling_parallax_init:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    li a0, 0
    la a1, scrolling_tile_blank
    li a2, 0
    li a3, 0
    call prg32_tile_define

    li a0, 1
    la a1, scrolling_tile_star
    li a2, 0xffff
    li a3, 0
    call prg32_tile_define

    li a0, 2
    la a1, scrolling_tile_ground
    li a2, 0x07e0
    li a3, 0x001f
    call prg32_tile_define

    li a0, 3
    la a1, scrolling_tile_cloud
    li a2, 0x07ff
    li a3, 0
    call prg32_tile_define

    li a0, 0
    li a1, 1
    call prg32_playfield_clear
    li a0, 1
    li a1, 0
    call prg32_playfield_clear

    li a0, 0
    li a1, 64
    li a2, 32
    call prg32_playfield_parallax
    li a0, 1
    li a1, 256
    li a2, 256
    call prg32_playfield_parallax

    li s0, 0
.Lscrolling_fill_ground:
    li a0, 1
    mv a1, s0
    li a2, 22
    li a3, 2
    call prg32_playfield_put

    li a0, 1
    mv a1, s0
    li a2, 23
    li a3, 2
    call prg32_playfield_put

    li a0, 1
    mv a1, s0
    li a2, 7
    li a3, 3
    call prg32_playfield_put

    addi s0, s0, 8
    li t0, 64
    blt s0, t0, .Lscrolling_fill_ground

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

scrolling_parallax_update:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    call prg32_ticks_ms
    srli s0, a0, 4
    la t0, scrolling_parallax_camera
    sw s0, 0(t0)

    mv a0, s0
    li a1, 0
    call prg32_playfield_camera

    li a0, 0
    li a1, 0
    srli a2, s0, 4
    call prg32_playfield_scroll

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

scrolling_parallax_draw:
    addi sp, sp, -16
    sw ra, 12(sp)

    li a0, 0
    call prg32_gfx_clear
    call prg32_playfield_draw_dual

    li a0, 8
    li a1, 8
    la a2, scrolling_parallax_title
    li a3, 0xffff
    li a4, 0
    call prg32_gfx_text8

    lw ra, 12(sp)
    addi sp, sp, 16
    ret
