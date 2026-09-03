#include "prg32.h"

uint8_t btn_a_pressed;
static char random_number[3];

void random_number_init(void) {
    uint32_t n = prg32_random_number(1, 90);
    random_number[0] = ' ';
    random_number[1] = ' ';
    random_number[2] = 0;
    if (n > 10) {
        random_number[0] = '0' + (n / 10);
        n -= (n / 10) * 10;
    }
    random_number[1] = '0' + n;
}

void random_number_update(void) {
    uint32_t input = prg32_input_read();
    if (!(input & PRG32_BTN_A)) {
        btn_a_pressed = 0;
        return;
    }

    if (btn_a_pressed) {
        return;
    }
    
    btn_a_pressed = 1;
    random_number_init();
}

void random_number_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "RANDOM NUMBER", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 30, random_number, PRG32_COLOR_RED, 0);
    prg32_gfx_text8(8, 50, "Press A to generate a new number", PRG32_COLOR_CYAN, 0);
}
