#include "prg32.h"

static int ship_x;
static int alien_x;
static int alien_dx;
static int bullet_y;

void space_invaders_c_init(void) {
    ship_x = 144;
    alien_x = 32;
    alien_dx = 1;
    bullet_y = -1;
}

void space_invaders_c_update(void) {
    uint32_t input = prg32_input_read();
    if (input & PRG32_BTN_LEFT) {
        ship_x -= 3;
    }
    if (input & PRG32_BTN_RIGHT) {
        ship_x += 3;
    }
    if ((input & PRG32_BTN_A) && bullet_y < 0) {
        bullet_y = 168;
    }
    if (ship_x < 0) {
        ship_x = 0;
    }
    if (ship_x > 288) {
        ship_x = 288;
    }

    alien_x += alien_dx;
    if (alien_x < 16 || alien_x > 272) {
        alien_dx = -alien_dx;
    }
    if (bullet_y >= 0) {
        bullet_y -= 5;
    }
}

void space_invaders_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_rect(ship_x, 180, 32, 8, PRG32_COLOR_GREEN);
    prg32_gfx_rect(ship_x + 12, 172, 8, 8, PRG32_COLOR_GREEN);
    prg32_gfx_rect(alien_x, 48, 32, 16, PRG32_COLOR_MAGENTA);
    if (bullet_y >= 0) {
        prg32_gfx_rect(ship_x + 15, bullet_y, 2, 8, PRG32_COLOR_YELLOW);
    }
    prg32_gfx_text8(8, 8, "INVADERS C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
}
