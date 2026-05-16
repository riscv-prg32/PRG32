/*
 * PLATFORMER GRAPHICS for PRG32.
 *
 * Small Tilengine-inspired platform demo: independent parallax layer, foreground
 * collision layer, gravity, jump, camera follow, and simple player animation.
 */
.option norelax

.equ ACTOR_X, 0
.equ ACTOR_Y, 4
.equ ACTOR_STATE, 20

.section .bss
.balign 4
platformer_graphics_actor: .space 24

.section .data
platformer_graphics_frame: .word 0

.section .rodata
platformer_graphics_title: .asciz "PLATFORMER"
tile_empty: .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
tile_cloud: .byte 0x00,0x18,0x3c,0x7e,0xff,0x7e,0x3c,0x00
tile_ground: .byte 0xff,0x81,0xbd,0xa5,0xa5,0xbd,0x81,0xff
tile_grass: .byte 0xff,0xff,0x81,0xbd,0xa5,0xbd,0x81,0xff
tile_hazard: .byte 0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81

.balign 2
player_frames:
    .half 0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff
    .half 0xffff,0x07e0,0xffff,0xffff,0xffff,0xffff,0x07e0,0xffff
    .half 0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0
    .half 0x07e0,0xffff,0xf800,0xffff,0xffff,0xf800,0xffff,0x07e0
    .half 0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0
    .half 0xffff,0x07e0,0xffff,0xffff,0xffff,0xffff,0x07e0,0xffff
    .half 0xffff,0xffff,0x001f,0xffff,0xffff,0x001f,0xffff,0xffff
    .half 0xffff,0x001f,0xffff,0xffff,0xffff,0xffff,0x001f,0xffff

    .half 0xffff,0xffff,0xffe0,0xffe0,0xffe0,0xffe0,0xffff,0xffff
    .half 0xffff,0xffe0,0xffff,0xffff,0xffff,0xffff,0xffe0,0xffff
    .half 0xffe0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffe0
    .half 0xffe0,0xffff,0xf800,0xffff,0xffff,0xf800,0xffff,0xffe0
    .half 0xffe0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffe0
    .half 0xffff,0xffe0,0xffff,0xffff,0xffff,0xffff,0xffe0,0xffff
    .half 0x001f,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x001f
    .half 0xffff,0x001f,0xffff,0xffff,0xffff,0xffff,0x001f,0xffff

.section .text
.global platformer_graphics_init
.global platformer_graphics_update
.global platformer_graphics_draw

platformer_graphics_init:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    li a0, 0
    la a1, tile_empty
    li a2, 0
    li a3, 0
    call prg32_tile_define

    li a0, 1
    la a1, tile_cloud
    li a2, 0xffff
    li a3, 0x001f
    call prg32_tile_define

    li a0, 2
    la a1, tile_ground
    li a2, 0x07e0
    li a3, 0x001f
    call prg32_tile_define

    li a0, 3
    la a1, tile_grass
    li a2, 0xffff
    li a3, 0x07e0
    call prg32_tile_define

    li a0, 4
    la a1, tile_hazard
    li a2, 0xf800
    li a3, 0
    call prg32_tile_define

    li a0, 2
    li a1, 1
    call prg32_platform_tile_flags
    li a0, 3
    li a1, 2
    call prg32_platform_tile_flags
    li a0, 4
    li a1, 5
    call prg32_platform_tile_flags

    li a0, 0
    li a1, 0
    call prg32_playfield_clear
    li a0, 1
    li a1, 0
    call prg32_playfield_clear

    li a0, 0
    li a1, 96
    li a2, 128
    call prg32_playfield_parallax

    li s0, 0
.Lplatformer_graphics_clouds:
    li a0, 0
    mv a1, s0
    li a2, 5
    li a3, 1
    call prg32_playfield_put
    addi s0, s0, 8
    li t0, 64
    blt s0, t0, .Lplatformer_graphics_clouds

    li s0, 0
.Lplatformer_graphics_ground:
    li a0, 1
    mv a1, s0
    li a2, 23
    li a3, 2
    call prg32_playfield_put
    li a0, 1
    mv a1, s0
    li a2, 22
    li a3, 3
    call prg32_playfield_put
    addi s0, s0, 1
    li t0, 64
    blt s0, t0, .Lplatformer_graphics_ground

    li s0, 10
.Lplatformer_graphics_platform:
    li a0, 1
    mv a1, s0
    li a2, 16
    li a3, 3
    call prg32_playfield_put
    addi s0, s0, 1
    li t0, 20
    blt s0, t0, .Lplatformer_graphics_platform

    li a0, 1
    li a1, 28
    li a2, 21
    li a3, 4
    call prg32_playfield_put

    la a0, platformer_graphics_actor
    li a1, 1
    li a2, 32
    li a3, 120
    li a4, 8
    li a5, 8
    call prg32_platform_actor_init

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

platformer_graphics_update:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    call prg32_input_read
    mv s0, a0
    la a0, platformer_graphics_actor
    mv a1, s0
    li a2, 2
    li a3, -7
    li a4, 1
    li a5, 5
    call prg32_platform_actor_step

    la a0, platformer_graphics_actor
    li a1, 56
    li a2, 32
    call prg32_platform_camera_follow

    call prg32_ticks_ms
    li a1, 2
    li a2, 140
    call prg32_sprite_anim_frame
    la t0, platformer_graphics_frame
    sw a0, 0(t0)

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

platformer_graphics_draw:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    sw s1, 4(sp)
    sw s2, 0(sp)

    li a0, 0
    call prg32_gfx_clear
    call prg32_playfield_draw_dual

    la s0, platformer_graphics_actor
    lw s1, ACTOR_X(s0)
    call prg32_playfield_camera_x
    sub s1, s1, a0
    lw s2, ACTOR_Y(s0)
    call prg32_playfield_camera_y
    mv a1, s2
    sub a1, a1, a0
    mv a0, s1
    li a2, 8
    li a3, 8
    la a4, player_frames
    la t0, platformer_graphics_frame
    lw a5, 0(t0)
    li a6, 0xffff
    call prg32_sprite_draw_frame

    li a0, 8
    li a1, 8
    la a2, platformer_graphics_title
    li a3, 0xffff
    li a4, 0
    call prg32_gfx_text8

    lw s2, 0(sp)
    lw s1, 4(sp)
    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
