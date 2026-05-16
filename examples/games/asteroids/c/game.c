#include "prg32.h"

static int ship_x;
static int ship_y;
static int asteroid_x;
static int asteroid_y;

void asteroids_c_init(void) {
    ship_x = 152;
    ship_y = 96;
    asteroid_x = 280;
    asteroid_y = 48;
}

void asteroids_c_update(void) {
    uint32_t input = prg32_input_read();
    if (input & PRG32_BTN_LEFT) {
        ship_x -= 2;
    }
    if (input & PRG32_BTN_RIGHT) {
        ship_x += 2;
    }
    if (input & PRG32_BTN_UP) {
        ship_y -= 2;
    }
    if (input & PRG32_BTN_DOWN) {
        ship_y += 2;
    }
    if (ship_x < 0) {
        ship_x = 0;
    }
    if (ship_x > 304) {
        ship_x = 304;
    }
    if (ship_y < 16) {
        ship_y = 16;
    }
    if (ship_y > 184) {
        ship_y = 184;
    }
    asteroid_x -= 2;
    if (asteroid_x < -16) {
        asteroid_x = 320;
    }
}

void asteroids_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_rect(ship_x + 6, ship_y, 4, 16, PRG32_COLOR_CYAN);
    prg32_gfx_rect(ship_x, ship_y + 8, 16, 8, PRG32_COLOR_CYAN);
    prg32_gfx_rect(asteroid_x, asteroid_y, 16, 16, PRG32_COLOR_YELLOW);
    prg32_gfx_text8(8, 8, "ASTEROIDS C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
}
