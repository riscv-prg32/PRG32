/*
 * PLATFORMER ASCII for PRG32.
 *
 * Debug view for platform movement. It uses the same platform actor helper as
 * the graphics version, then prints x, y, velocity, and collision state.
 */
.option norelax

.equ ACTOR_X, 0
.equ ACTOR_Y, 4
.equ ACTOR_VX, 8
.equ ACTOR_VY, 12
.equ ACTOR_STATE, 20

.section .bss
.balign 4
platformer_ascii_actor: .space 24

.section .rodata
platformer_ascii_title: .asciz "PLATFORMER ASCII\n"
platformer_ascii_help:  .asciz "LEFT RIGHT A/UP\n"
platformer_ascii_x:     .asciz " X "
platformer_ascii_y:     .asciz " Y "
platformer_ascii_vy:    .asciz " VY "
platformer_ascii_state: .asciz " STATE "

.section .text
.global platformer_ascii_init
.global platformer_ascii_update
.global platformer_ascii_draw

platformer_ascii_init:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    li a0, 1
    li a1, 1
    call prg32_platform_tile_flags

    li a0, 1
    li a1, 0
    call prg32_playfield_clear

    li s0, 0
.Lplatformer_ascii_ground:
    li a0, 1
    mv a1, s0
    li a2, 23
    li a3, 1
    call prg32_playfield_put
    addi s0, s0, 1
    li t0, 64
    blt s0, t0, .Lplatformer_ascii_ground

    la a0, platformer_ascii_actor
    li a1, 1
    li a2, 32
    li a3, 120
    li a4, 12
    li a5, 14
    call prg32_platform_actor_init

    call prg32_console_clear
    la a0, platformer_ascii_title
    call prg32_console_write
    la a0, platformer_ascii_help
    call prg32_console_write

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

platformer_ascii_update:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    call prg32_input_read
    mv s0, a0
    la a0, platformer_ascii_actor
    mv a1, s0
    li a2, 2
    li a3, -7
    li a4, 1
    li a5, 5
    call prg32_platform_actor_step

    la a0, platformer_ascii_actor
    li a1, 48
    li a2, 32
    call prg32_platform_camera_follow

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

platformer_ascii_draw:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)

    call prg32_console_clear
    la a0, platformer_ascii_title
    call prg32_console_write
    la a0, platformer_ascii_help
    call prg32_console_write

    la s0, platformer_ascii_actor

    la a0, platformer_ascii_x
    call prg32_console_write
    lw a0, ACTOR_X(s0)
    call prg32_console_hex32

    la a0, platformer_ascii_y
    call prg32_console_write
    lw a0, ACTOR_Y(s0)
    call prg32_console_hex32

    la a0, platformer_ascii_vy
    call prg32_console_write
    lw a0, ACTOR_VY(s0)
    call prg32_console_hex32

    la a0, platformer_ascii_state
    call prg32_console_write
    lhu a0, ACTOR_STATE(s0)
    call prg32_console_hex32

    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
