#include "prg32.h"

static uint32_t bar_x;

void splash_screen_c_init(void) {
    bar_x = 0;
    prg32_splash_show_game("C GALAXY",
                           "BOOTING GAME STATE",
                           900,
                           PRG32_COLOR_BLACK,
                           PRG32_COLOR_WHITE,
                           PRG32_COLOR_CYAN);
}

void splash_screen_c_update(void) {
    bar_x = 88 + ((prg32_ticks_ms() >> 4) & 0x7f);
}

void splash_screen_c_draw(void) {
    prg32_splash_draw_game("C GALAXY",
                           "SPLASH API FROM C",
                           PRG32_COLOR_BLACK,
                           PRG32_COLOR_WHITE,
                           PRG32_COLOR_YELLOW);
    prg32_gfx_rect((int)bar_x, 154, 32, 6, PRG32_COLOR_GREEN);
}
