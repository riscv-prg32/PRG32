#include "prg32.h"

static const uint8_t blank[8] = {0};
static const uint8_t checker[8] = {
    0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55,
};
static const uint8_t platform[8] = {
    0xff, 0xff, 0x81, 0xbd, 0xbd, 0x81, 0xff, 0xff,
};

void dual_playfield_c_init(void) {
    prg32_tile_define(0, blank, PRG32_COLOR_BLACK, PRG32_COLOR_BLACK);
    prg32_tile_define(1, checker, PRG32_COLOR_BLUE, PRG32_COLOR_BLACK);
    prg32_tile_define(2, platform, PRG32_COLOR_WHITE, PRG32_COLOR_RED);
    prg32_playfield_clear(0, 1);
    prg32_playfield_clear(1, 0);
    for (uint8_t x = 0; x < PRG32_PLAYFIELD_COLS; x += 2) {
        prg32_playfield_put(1, x, 16, 2);
        prg32_playfield_put(1, x, 17, 2);
    }
}

void dual_playfield_c_update(void) {
    int scroll = (int)(prg32_ticks_ms() >> 5);
    prg32_playfield_scroll(0, scroll / 2, 0);
    prg32_playfield_scroll(1, scroll, 0);
}

void dual_playfield_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_playfield_draw_dual();
    prg32_gfx_text8(8, 8, "DUAL C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
}
