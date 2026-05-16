#include "prg32.h"

static const uint8_t blank[8] = {0};
static const uint8_t star[8] = {
    0x00, 0x10, 0x00, 0x44, 0x00, 0x10, 0x00, 0x00,
};
static const uint8_t ground[8] = {
    0xff, 0x81, 0xbd, 0xa5, 0xbd, 0x81, 0xff, 0xff,
};
static uint32_t camera;

void scrolling_parallax_c_init(void) {
    prg32_tile_define(0, blank, PRG32_COLOR_BLACK, PRG32_COLOR_BLACK);
    prg32_tile_define(1, star, PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
    prg32_tile_define(2, ground, PRG32_COLOR_GREEN, PRG32_COLOR_BLUE);
    prg32_playfield_clear(0, 1);
    prg32_playfield_clear(1, 0);
    prg32_playfield_parallax(0, 64, 32);
    prg32_playfield_parallax(1, PRG32_PARALLAX_1X, PRG32_PARALLAX_1X);
    for (uint8_t x = 0; x < PRG32_PLAYFIELD_COLS; ++x) {
        prg32_playfield_put(1, x, 22, 2);
        prg32_playfield_put(1, x, 23, 2);
    }
}

void scrolling_parallax_c_update(void) {
    camera = prg32_ticks_ms() >> 4;
    prg32_playfield_camera((int)camera, 0);
}

void scrolling_parallax_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_playfield_draw_dual();
    prg32_gfx_text8(8, 8, "PARALLAX C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
}
