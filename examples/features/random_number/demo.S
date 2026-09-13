.option norelax

.equ BUTTON_A_MASK, (1 << 4)

.section .bss
button_was_pressed: .zero 1
number_text:       .zero 3

.section .rodata
title: .asciz "RANDOM NUMBER"
hint:  .asciz "Press A to generate a new number"

.section .text
.global random_number_init
.global random_number_update
.global random_number_draw

random_number_init:
    addi sp, sp, -16
    sw   ra, 12(sp)

    # The inclusive bounds are passed in a0 and a1; the result returns in a0.
    li   a0, 1
    li   a1, 90
    call prg32_random_number

    # Format the two-digit result as " 7" or "42", followed by a terminator.
    la   t0, number_text
    li   t1, 10
    divu t2, a0, t1
    remu t3, a0, t1
    li   t4, ' '
    beqz t2, .Lrandom_write_digits
    addi t4, t2, '0'
.Lrandom_write_digits:
    sb   t4, 0(t0)
    addi t3, t3, '0'
    sb   t3, 1(t0)
    sb   zero, 2(t0)

    lw   ra, 12(sp)
    addi sp, sp, 16
    ret

random_number_update:
    addi sp, sp, -16
    sw   ra, 12(sp)

    call prg32_input_read
    andi t0, a0, BUTTON_A_MASK
    la   t1, button_was_pressed
    beqz t0, .Lrandom_button_released

    lbu  t2, 0(t1)
    bnez t2, .Lrandom_update_done
    li   t2, 1
    sb   t2, 0(t1)
    call random_number_init
    j    .Lrandom_update_done

.Lrandom_button_released:
    sb   zero, 0(t1)
.Lrandom_update_done:
    lw   ra, 12(sp)
    addi sp, sp, 16
    ret

random_number_draw:
    addi sp, sp, -16
    sw   ra, 12(sp)

    li   a0, 0
    call prg32_gfx_clear

    li   a0, 8
    li   a1, 8
    la   a2, title
    li   a3, 0xffff
    li   a4, 0
    call prg32_gfx_text8

    li   a0, 8
    li   a1, 30
    la   a2, number_text
    li   a3, 0xf800
    li   a4, 0
    call prg32_gfx_text8

    li   a0, 8
    li   a1, 50
    la   a2, hint
    li   a3, 0x07ff
    li   a4, 0
    call prg32_gfx_text8

    lw   ra, 12(sp)
    addi sp, sp, 16
    ret
