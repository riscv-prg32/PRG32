.option norelax

.section .data
btn_a_pressed:  .byte 0
random_number:  .byte 0x00,0x00,0x00

.section .rodata
title:  .asciz "RANDOM NUMBER"
hint:   .asciz "Press A to generate a new number"

.section .text
.global random_number_init
.global random_number_update
.global random_number_draw
    

random_number_init:
    addi sp, sp, -8
    sw   ra, 4(sp)

    # generate a random number between 1 and 90
    li      a0, 1
    li      a1, 90
    call    prg32_random_number

    # now we convert the number to a "string"
    la  t0, random_number  # t0 containns the addres of "random_number" string 

    # first we set the string as space, space, 0
    li  t1, ' '
    sb  t1, 0(t0)
    sb  t1, 1(t0) 
    li  t1, 0
    sb  t1, 2(t0)

    # when the number is smaller than 10 we skip converting the first digit
    li  t1, 10
    blt a0, t1, .less_than_10

    # convert the first digit to char, we divide the generated number (a0) by then and add it '0'
    div     t2, a0, t1      # t2 = a0 / 10
    addi    t3, t2, '0'     # t3 = t2 + '0' 
    sb      t3, 0(t0)       # random_number[0] = t1

    # now we set in a0 just the second digit
    mul t2, t2, t1  # t2 *= 10
    sub a0, a0, t2  # a0 -= t2 

.less_than_10:
    # convert the second digit
    addi    t2, a0, '0'
    sb      t2, 1(t0)

    # return
    lw   ra, 4(sp)
    addi sp, sp, 8
    ret

random_number_update:
    addi    sp, sp, -8
    sw      ra, 4(sp)

    # a0 will contain the current input
    call    prg32_input_read  # a0 has input

    # check if BTN A is pressed, if not it sets btn_a_pressed as 0 and exits
    andi    t0, a0, 0x10            # t0 = input & PRG32_BTN_A
    beqz    t0, .btn_a_not_pressed

    # check if it was pressed from previous cycle, if it was just return
    la      t0, btn_a_pressed
    lb      t1, 0(t0)
    bnez    t1, .return_update

    # set btn_a_pressed as 1
    li  t1, 1
    sb  t1, 0(t0)
 
    # generate a number and exit
    call    random_number_init
    j       .return_update
    
.btn_a_not_pressed:
    la t0, btn_a_pressed
    li t1, 0
    sb t1, 0(t0)

.return_update:
    lw   ra, 4(sp)
    addi sp, sp, 8
    ret

random_number_draw:
    addi sp, sp, -8
    sw   ra, 4(sp)

    li a0, 0
    call prg32_gfx_clear

    li a0, 8
    li a1, 8
    la a2, title
    li a3, 0xffff
    li a4, 0
    call prg32_gfx_text8

 
    li a0, 8
    li a1, 30
    la a2, random_number
    li a3, 0xf800
    li a4, 0
    call prg32_gfx_text8

    li a0, 8
    li a1, 50
    la a2, hint
    li a3, 0x07ff
    li a4, 0
    call prg32_gfx_text8

    lw   ra, 4(sp)
    addi sp, sp, 8
    ret