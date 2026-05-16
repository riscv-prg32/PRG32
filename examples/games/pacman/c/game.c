#include "prg32.h"

static int pac_x;
static int ghost_x;
static uint8_t pellets;

void pacman_c_init(void) {
    pac_x = 24;
    ghost_x = 240;
    pellets = 0xff;
}

void pacman_c_update(void) {
    uint32_t input = prg32_input_read();
    if (input & PRG32_BTN_LEFT) {
        pac_x -= 2;
    }
    if (input & PRG32_BTN_RIGHT) {
        pac_x += 2;
    }
    if (pac_x < 16) {
        pac_x = 16;
    }
    if (pac_x > 288) {
        pac_x = 288;
    }
    ghost_x += ghost_x > pac_x ? -1 : 1;
    pellets &= (uint8_t)~(1u << ((unsigned)pac_x / 40u));
}

void pacman_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_rect(8, 80, 304, 8, PRG32_COLOR_BLUE);
    prg32_gfx_rect(8, 128, 304, 8, PRG32_COLOR_BLUE);
    for (int i = 0; i < 8; ++i) {
        if (pellets & (1u << i)) {
            prg32_gfx_rect(i * 40 + 20, 104, 4, 4, PRG32_COLOR_WHITE);
        }
    }
    prg32_gfx_rect(pac_x, 96, 16, 16, PRG32_COLOR_YELLOW);
    prg32_gfx_rect(ghost_x, 96, 16, 16, PRG32_COLOR_RED);
    prg32_gfx_text8(8, 8, "PACMAN C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
}
