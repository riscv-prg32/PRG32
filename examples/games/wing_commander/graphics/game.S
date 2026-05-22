/*
 * WING COMMANDER GRAPHICS for PRG32.
 *
 * Compact dual-playfield cockpit demo. Layer 0 scrolls stars outside the
 * window; layer 1 remains fixed as the cockpit/dashboard overlay.
 */
.option norelax

.equ BTN_LEFT,  1
.equ BTN_RIGHT, 2
.equ BTN_UP,    4
.equ BTN_DOWN,  8
.equ TILE_STAR,  1
.equ TILE_PANEL, 2
.equ TILE_EDGE,  3

.section .data
wing_commander_graphics_x:     .word 160
wing_commander_graphics_y:     .word 94
wing_commander_graphics_frame: .word 0

.section .rodata
wing_commander_graphics_title: .asciz "WING COMMANDER ASM"
tile_star:  .byte 0x00,0x10,0x38,0x10,0x00,0x00,0x00,0x00
tile_panel: .byte 0xff,0xbd,0x81,0xa5,0x81,0xbd,0x81,0xff
tile_edge:  .byte 0x18,0x3c,0x7e,0xff,0xff,0x7e,0x3c,0x18

.section .text
.global wing_commander_graphics_init
.global wing_commander_graphics_update
.global wing_commander_graphics_draw

wing_commander_graphics_init:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    li a0, 0
    li a1, 0
    li a2, 0
    li a3, 0
    call prg32_tile_define
    li a0, TILE_STAR
    la a1, tile_star
    li a2, 0xffff
    li a3, 0
    call prg32_tile_define
    li a0, TILE_PANEL
    la a1, tile_panel
    li a2, 0x7bef
    li a3, 0x2104
    call prg32_tile_define
    li a0, TILE_EDGE
    la a1, tile_edge
    li a2, 0x07ff
    li a3, 0x2104
    call prg32_tile_define

    li a0, 0
    li a1, 0
    call prg32_playfield_clear
    li a0, 1
    li a1, 0
    call prg32_playfield_clear

    li s0, 0
.Lwing_stars:
    li a0, 0
    mv a1, s0
    li t0, 17
    mul a1, a1, t0
    addi a1, a1, 5
    li t0, 64
    remu a1, a1, t0
    mv a2, s0
    li t0, 23
    mul a2, a2, t0
    addi a2, a2, 3
    li t0, 32
    remu a2, a2, t0
    li a3, TILE_STAR
    call prg32_playfield_put
    addi s0, s0, 1
    li t0, 64
    blt s0, t0, .Lwing_stars

    li s0, 0
.Lwing_panel_cols:
    li a0, 1
    mv a1, s0
    li a2, 22
    li a3, TILE_PANEL
    call prg32_playfield_put
    li a0, 1
    mv a1, s0
    li a2, 23
    li a3, TILE_PANEL
    call prg32_playfield_put
    li a0, 1
    mv a1, s0
    li a2, 24
    li a3, TILE_PANEL
    call prg32_playfield_put
    addi s0, s0, 1
    li t0, 40
    blt s0, t0, .Lwing_panel_cols

    li s0, 0
.Lwing_side_rows:
    li a0, 1
    li a1, 0
    mv a2, s0
    addi a2, a2, 8
    li a3, TILE_PANEL
    call prg32_playfield_put
    li a0, 1
    li a1, 39
    mv a2, s0
    addi a2, a2, 8
    li a3, TILE_PANEL
    call prg32_playfield_put
    addi s0, s0, 1
    li t0, 14
    blt s0, t0, .Lwing_side_rows

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

wing_commander_graphics_update:
    addi sp, sp, -16
    sw ra, 12(sp)

    call prg32_input_read
    mv t0, a0
    la t1, wing_commander_graphics_x
    lw t2, 0(t1)
    andi t3, t0, BTN_LEFT
    beqz t3, .Lwing_no_left
    addi t2, t2, -4
.Lwing_no_left:
    andi t3, t0, BTN_RIGHT
    beqz t3, .Lwing_no_right
    addi t2, t2, 4
.Lwing_no_right:
    li t3, 42
    bge t2, t3, .Lwing_x_min_ok
    mv t2, t3
.Lwing_x_min_ok:
    li t3, 278
    ble t2, t3, .Lwing_x_max_ok
    mv t2, t3
.Lwing_x_max_ok:
    sw t2, 0(t1)

    la t1, wing_commander_graphics_y
    lw t2, 0(t1)
    andi t3, t0, BTN_UP
    beqz t3, .Lwing_no_up
    addi t2, t2, -3
.Lwing_no_up:
    andi t3, t0, BTN_DOWN
    beqz t3, .Lwing_no_down
    addi t2, t2, 3
.Lwing_no_down:
    li t3, 44
    bge t2, t3, .Lwing_y_min_ok
    mv t2, t3
.Lwing_y_min_ok:
    li t3, 150
    ble t2, t3, .Lwing_y_max_ok
    mv t2, t3
.Lwing_y_max_ok:
    sw t2, 0(t1)

    la t1, wing_commander_graphics_frame
    lw t2, 0(t1)
    addi t2, t2, 1
    sw t2, 0(t1)

    lw ra, 12(sp)
    addi sp, sp, 16
    ret

wing_commander_graphics_draw:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    sw s1, 4(sp)

    la t0, wing_commander_graphics_frame
    lw a2, 0(t0)
    li t1, -3
    mul a2, a2, t1
    li a0, 0
    li a1, 0
    call prg32_playfield_scroll
    call prg32_playfield_draw_dual

    la t0, wing_commander_graphics_x
    lw s0, 0(t0)
    la t0, wing_commander_graphics_y
    lw s1, 0(t0)

    addi a0, s0, -14
    mv a1, s1
    li a2, 10
    li a3, 2
    li a4, 0x07e0
    call prg32_gfx_rect
    addi a0, s0, 4
    mv a1, s1
    li a2, 10
    li a3, 2
    li a4, 0x07e0
    call prg32_gfx_rect
    mv a0, s0
    addi a1, s1, -14
    li a2, 2
    li a3, 10
    li a4, 0x07e0
    call prg32_gfx_rect
    mv a0, s0
    addi a1, s1, 4
    li a2, 2
    li a3, 10
    li a4, 0x07e0
    call prg32_gfx_rect

    li a0, 8
    li a1, 8
    la a2, wing_commander_graphics_title
    li a3, 0xffff
    li a4, 0
    call prg32_gfx_text8

    lw s1, 4(sp)
    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
