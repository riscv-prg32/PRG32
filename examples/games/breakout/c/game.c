#include "prg32.h"

static int paddle_x;
static int ball_x;
static int ball_y;
static int ball_dx;
static int ball_dy;
static uint8_t bricks;

void breakout_c_init(void) {
    paddle_x = 128;
    ball_x = 150;
    ball_y = 120;
    ball_dx = 2;
    ball_dy = -2;
    bricks = 0xff;
}

void breakout_c_update(void) {
    uint32_t input = prg32_input_read();
    if (input & PRG32_BTN_LEFT) {
        paddle_x -= 3;
    }
    if (input & PRG32_BTN_RIGHT) {
        paddle_x += 3;
    }
    if (paddle_x < 0) {
        paddle_x = 0;
    }
    if (paddle_x > 256) {
        paddle_x = 256;
    }

    ball_x += ball_dx;
    ball_y += ball_dy;
    if (ball_x < 0 || ball_x > 312) {
        ball_dx = -ball_dx;
    }
    if (ball_y < 0 || ball_y > 192) {
        ball_dy = -ball_dy;
    }
    if (prg32_sprite_hitbox(ball_x, ball_y, 8, 8, paddle_x, 184, 64, 8)) {
        ball_dy = -2;
    }
    if (ball_y < 48 && ball_x >= 0 && ball_x < 320 && bricks) {
        bricks &= (uint8_t)~(1u << ((unsigned)ball_x / 40u));
        ball_dy = 2;
    }
}

void breakout_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    for (int i = 0; i < 8; ++i) {
        if (bricks & (1u << i)) {
            prg32_gfx_rect(i * 40 + 2, 32, 36, 10, PRG32_COLOR_CYAN);
        }
    }
    prg32_gfx_rect(paddle_x, 184, 64, 8, PRG32_COLOR_WHITE);
    prg32_gfx_rect(ball_x, ball_y, 8, 8, PRG32_COLOR_YELLOW);
    prg32_gfx_text8(8, 8, "BREAKOUT C", PRG32_COLOR_GREEN, PRG32_COLOR_BLACK);
}
