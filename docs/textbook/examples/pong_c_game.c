#include "prg32.h"

static int paddle_x;
static int paddle2_x;
static int ball_x;
static int ball_y;
static int ball_dx;
static int ball_dy;

void pong_c_init(void) {
    paddle_x = 128;
    paddle2_x = 128;
    ball_x = 156;
    ball_y = 96;
    ball_dx = 2;
    ball_dy = 1;
}

void pong_c_update(void) {
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

    uint32_t input2 = prg32_input_read_player(2);
    if (input2 & PRG32_BTN_LEFT) {
        paddle2_x -= 3;
    } else if (input2 & PRG32_BTN_RIGHT) {
        paddle2_x += 3;
    } else if (ball_x + 4 < paddle2_x + 32) {
        paddle2_x -= 1;
    } else if (ball_x + 4 > paddle2_x + 32) {
        paddle2_x += 1;
    }
    if (paddle2_x < 0) {
        paddle2_x = 0;
    }
    if (paddle2_x > 256) {
        paddle2_x = 256;
    }

    ball_x += ball_dx;
    ball_y += ball_dy;
    if (ball_x < 0 || ball_x > 312) {
        ball_dx = -ball_dx;
    }
    if (prg32_sprite_hitbox(ball_x, ball_y, 8, 8, paddle2_x, 4, 64, 8)) {
        ball_dy = 1;
    }
    if (prg32_sprite_hitbox(ball_x, ball_y, 8, 8, paddle_x, 188, 64, 8)) {
        ball_dy = -1;
    }
    if (ball_y < 0 || ball_y > 192) {
        ball_dy = -ball_dy;
    }
}

void pong_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_rect(paddle2_x, 4, 64, 8, PRG32_COLOR_CYAN);
    prg32_gfx_rect(paddle_x, 188, 64, 8, PRG32_COLOR_WHITE);
    prg32_gfx_rect(ball_x, ball_y, 8, 8, PRG32_COLOR_YELLOW);
    prg32_gfx_text8(8, 96, "PONG C P1/P2", PRG32_COLOR_GREEN, PRG32_COLOR_BLACK);
}
