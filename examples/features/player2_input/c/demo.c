#include "prg32.h"

static int p1_x;
static int p2_x;

static int clamp_x(int x) {
    if (x < 0) {
        return 0;
    }
    if (x > 288) {
        return 288;
    }
    return x;
}

void player2_input_c_init(void) {
    p1_x = 40;
    p2_x = 240;
}

void player2_input_c_update(void) {
    uint32_t p1 = prg32_input_read_player(1);
    uint32_t p2 = prg32_input_read_player(2);
    if (p1 & PRG32_BTN_LEFT) {
        p1_x -= 3;
    }
    if (p1 & PRG32_BTN_RIGHT) {
        p1_x += 3;
    }
    if (p2 & PRG32_BTN_LEFT) {
        p2_x -= 3;
    }
    if (p2 & PRG32_BTN_RIGHT) {
        p2_x += 3;
    }
    p1_x = clamp_x(p1_x);
    p2_x = clamp_x(p2_x);
}

void player2_input_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "PLAYER 2 INPUT", PRG32_COLOR_WHITE, 0);
    prg32_gfx_rect(p1_x, 72, 32, 16, PRG32_COLOR_GREEN);
    prg32_gfx_rect(p2_x, 112, 32, 16, PRG32_COLOR_CYAN);
    prg32_gfx_text8(8, 72, "P1", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 112, "P2", PRG32_COLOR_CYAN, 0);
}
