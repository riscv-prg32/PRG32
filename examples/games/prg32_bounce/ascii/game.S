/* PRG32 BOUNCE ASCII: trace the Start-gated state before drawing pixels. */
.option norelax

.section .data
prg32_bounce_ascii_started: .word 0
prg32_bounce_ascii_x: .word 0
prg32_bounce_ascii_title: .asciz "PRG32 BOUNCE - PRESS START"
prg32_bounce_ascii_ball: .asciz "                 (PRG32)"

.section .text
.global prg32_bounce_ascii_init
.global prg32_bounce_ascii_update
.global prg32_bounce_ascii_draw

prg32_bounce_ascii_init:
    la t0, prg32_bounce_ascii_started
    sw zero, 0(t0)
    la t0, prg32_bounce_ascii_x
    sw zero, 0(t0)
    ret

prg32_bounce_ascii_update:
    addi sp, sp, -16
    sw ra, 12(sp)
    call prg32_input_read
    la t0, prg32_bounce_ascii_started
    lw t1, 0(t0)
    bnez t1, .Lprg32_bounce_ascii_move
    andi t2, a0, 64                 /* PRG32_BTN_START */
    beqz t2, .Lprg32_bounce_ascii_done
    li t1, 1
    sw t1, 0(t0)
.Lprg32_bounce_ascii_move:
    la t0, prg32_bounce_ascii_x
    lw t1, 0(t0)
    addi t1, t1, 1
    li t2, 40
    blt t1, t2, .Lprg32_bounce_ascii_store
    li t1, 0
.Lprg32_bounce_ascii_store:
    sw t1, 0(t0)
.Lprg32_bounce_ascii_done:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

prg32_bounce_ascii_draw:
    addi sp, sp, -16
    sw ra, 12(sp)
    call prg32_console_clear
    la t0, prg32_bounce_ascii_started
    lw t1, 0(t0)
    bnez t1, .Lprg32_bounce_ascii_playing
    la a0, prg32_bounce_ascii_title
    call prg32_console_write
    j .Lprg32_bounce_ascii_finish
.Lprg32_bounce_ascii_playing:
    la a0, prg32_bounce_ascii_ball
    call prg32_console_write
.Lprg32_bounce_ascii_finish:
    li a0, 10
    call prg32_console_putc
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
