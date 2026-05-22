#include "prg32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define DEMO_PAGE_COUNT 10
#define DEMO_FIELD_TOP 40
#define DEMO_FIELD_BOTTOM 184
#define DEMO_FRAME_MS 33

static const uint8_t tile_grid[8] = {
    0xff, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xff,
};

static const uint8_t tile_diag[8] = {
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};

static const uint8_t sprite_bits[8] = {
    0x18, 0x3c, 0x7e, 0xdb, 0xff, 0x24, 0x5a, 0xa5,
};

static void demo_prepare_playfields(void) {
    prg32_tile_define(0, NULL, PRG32_COLOR_BLACK, PRG32_COLOR_BLACK);
    prg32_tile_define(1, tile_grid, PRG32_COLOR_BLUE, PRG32_COLOR_BLACK);
    prg32_tile_define(2, tile_diag, PRG32_COLOR_MAGENTA, PRG32_COLOR_BLACK);
    prg32_tile_define(3, tile_grid, PRG32_COLOR_CYAN, PRG32_COLOR_BLACK);
    prg32_playfield_clear(0, 1);
    prg32_playfield_clear(1, 0);
    for (uint8_t y = 0; y < PRG32_PLAYFIELD_ROWS; ++y) {
        for (uint8_t x = 0; x < PRG32_PLAYFIELD_COLS; ++x) {
            if (((x + y) & 7u) == 0u) {
                prg32_playfield_put(0, x, y, 2);
            }
            if (((x * 3u + y) & 15u) == 0u) {
                prg32_playfield_put(1, x, y, 3);
            }
        }
    }
    prg32_playfield_parallax(0, PRG32_PARALLAX_1X / 2, PRG32_PARALLAX_1X / 2);
    prg32_playfield_parallax(1, PRG32_PARALLAX_1X, PRG32_PARALLAX_1X);
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        prg32_gfx_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int demo_abs(int value) {
    return value < 0 ? -value : value;
}

static int demo_clamp(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int rect_hit(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static int tri_wave(uint32_t frame, int period, int amplitude) {
    int p = (int)(frame % (uint32_t)period);
    int half = period / 2;
    if (p >= half) {
        p = period - p;
    }
    return (p * amplitude) / half;
}

static void wait_for_frame_target(uint32_t *next_ms) {
    uint32_t now = prg32_ticks_ms();
    if (!next_ms) {
        return;
    }
    if (*next_ms == 0) {
        *next_ms = now;
    }
    int32_t late_ms = (int32_t)(now - *next_ms);
    if (late_ms > (int32_t)(DEMO_FRAME_MS * 4u)) {
        *next_ms = now;
    } else {
        *next_ms += DEMO_FRAME_MS;
    }
    now = prg32_ticks_ms();
    if ((int32_t)(*next_ms - now) > 0) {
        vTaskDelay(pdMS_TO_TICKS(*next_ms - now));
    }
}

static void draw_title(const char *title, const char *subtitle) {
    prg32_gfx_text8(8, 8, title, PRG32_COLOR_WHITE, 0);
    if (subtitle) {
        prg32_gfx_text8(8, 24, subtitle, PRG32_COLOR_CYAN, 0);
    }
}

static void draw_footer(void) {
    prg32_gfx_text8(76, 188, "A BACK  SELECT/B NEXT", PRG32_COLOR_WHITE, 0);
}

static void draw_overview(uint32_t frame) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("PRG32 DEVICE DEMO", "GAME CONTENT IS 320x200");
    prg32_gfx_text8(8, 48, "SPLASH + SETUP USE 320x240", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 64, "STATUS BANDS ARE ABI OVERLAYS", PRG32_COLOR_GREEN, 0);
    for (int i = 0; i < 8; ++i) {
        int x = 24 + i * 34;
        int h = 20 + (int)((frame + (uint32_t)i * 9u) % 42u);
        uint16_t color = (i & 1) ? PRG32_COLOR_MAGENTA : PRG32_COLOR_BLUE;
        prg32_gfx_rect(x, 160 - h, 20, h, color);
    }
    prg32_sprite_draw_8x8(156, 92, sprite_bits, PRG32_COLOR_YELLOW, PRG32_COLOR_BLACK);
    draw_footer();
}

static void draw_graphics(uint32_t frame) {
    prg32_playfield_camera((int)(frame * 2u), (int)frame);
    prg32_playfield_draw_dual();
    int x = 20 + (int)((frame * 3u) % 260u);
    int y = 56 + (int)((frame * 2u) % 76u);
    prg32_sprite_draw_8x8(x, y, sprite_bits, PRG32_COLOR_YELLOW, PRG32_COLOR_BLACK);
    draw_title("SCROLLING + PLAYFIELDS", "SPRITES STAY INSIDE VIEWPORT");
    draw_footer();
}

static void draw_system(uint32_t frame) {
    char line[48];
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("FRAMEWORK CHECKLIST", "INPUT  AUDIO  WIFI  CARTS");
    prg32_gfx_text8(8, 56, "TILES  SPRITES  PLATFORM API", PRG32_COLOR_GREEN, 0);
    snprintf(line, sizeof(line), "DEFAULT CART SLOT: %d", prg32_cart_default_slot());
    prg32_gfx_text8(8, 88, line, PRG32_COLOR_CYAN, 0);
    snprintf(line, sizeof(line), "TICKS: %lu", (unsigned long)prg32_ticks_ms());
    prg32_gfx_text8(8, 104, line, PRG32_COLOR_CYAN, 0);
    int pulse = 20 + (int)(frame % 80u);
    prg32_gfx_rect(44, 146, pulse, 14, PRG32_COLOR_MAGENTA);
    prg32_gfx_rect(44 + pulse, 146, 100 - pulse, 14, PRG32_COLOR_BLUE);
    draw_footer();
}

typedef struct {
    int initialized;
    int ball_x;
    int ball_y;
    int ball_dx;
    int ball_dy;
    int left_y;
    int right_y;
    int left_score;
    int right_score;
} pong_demo_t;

static pong_demo_t pong_demo;

static void reset_pong_demo(void) {
    pong_demo.initialized = 1;
    pong_demo.ball_x = 156;
    pong_demo.ball_y = 104;
    pong_demo.ball_dx = 3;
    pong_demo.ball_dy = 2;
    pong_demo.left_y = 82;
    pong_demo.right_y = 82;
    pong_demo.left_score = 0;
    pong_demo.right_score = 0;
}

static void draw_pong(uint32_t frame) {
    if (!pong_demo.initialized) {
        reset_pong_demo();
    }

    int target_left = pong_demo.ball_y - 18;
    int target_right = pong_demo.ball_y - 18;
    if (pong_demo.left_y < target_left) pong_demo.left_y += 2;
    if (pong_demo.left_y > target_left) pong_demo.left_y -= 2;
    if (pong_demo.right_y < target_right) pong_demo.right_y += 2;
    if (pong_demo.right_y > target_right) pong_demo.right_y -= 2;
    pong_demo.left_y = demo_clamp(pong_demo.left_y, DEMO_FIELD_TOP + 8, DEMO_FIELD_BOTTOM - 52);
    pong_demo.right_y = demo_clamp(pong_demo.right_y, DEMO_FIELD_TOP + 8, DEMO_FIELD_BOTTOM - 52);

    pong_demo.ball_x += pong_demo.ball_dx;
    pong_demo.ball_y += pong_demo.ball_dy;
    if (pong_demo.ball_y <= DEMO_FIELD_TOP + 8 ||
        pong_demo.ball_y + 8 >= DEMO_FIELD_BOTTOM) {
        pong_demo.ball_dy = -pong_demo.ball_dy;
        pong_demo.ball_y = demo_clamp(pong_demo.ball_y,
                                      DEMO_FIELD_TOP + 8,
                                      DEMO_FIELD_BOTTOM - 8);
    }
    if (pong_demo.ball_dx < 0 &&
        rect_hit(pong_demo.ball_x, pong_demo.ball_y, 8, 8, 20, pong_demo.left_y, 8, 42)) {
        int hit = pong_demo.ball_y + 4 - (pong_demo.left_y + 21);
        pong_demo.ball_dx = 3;
        pong_demo.ball_dy = demo_clamp(hit / 7, -3, 3);
        if (pong_demo.ball_dy == 0) pong_demo.ball_dy = (frame & 1u) ? 1 : -1;
        pong_demo.ball_x = 28;
    }
    if (pong_demo.ball_dx > 0 &&
        rect_hit(pong_demo.ball_x, pong_demo.ball_y, 8, 8, 292, pong_demo.right_y, 8, 42)) {
        int hit = pong_demo.ball_y + 4 - (pong_demo.right_y + 21);
        pong_demo.ball_dx = -3;
        pong_demo.ball_dy = demo_clamp(hit / 7, -3, 3);
        if (pong_demo.ball_dy == 0) pong_demo.ball_dy = (frame & 1u) ? 1 : -1;
        pong_demo.ball_x = 284;
    }
    if (pong_demo.ball_x < 0 || pong_demo.ball_x > 312) {
        if (pong_demo.ball_x < 0) {
            pong_demo.right_score++;
            pong_demo.ball_dx = 3;
        } else {
            pong_demo.left_score++;
            pong_demo.ball_dx = -3;
        }
        pong_demo.ball_x = 156;
        pong_demo.ball_y = 104;
        pong_demo.ball_dy = (frame & 1u) ? 2 : -2;
    }

    char score[16];
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("PONG-INSPIRED DEMO", "BALL BOUNCES ON PADDLES + WALLS");
    for (int y = 44; y < 174; y += 16) {
        prg32_gfx_rect(158, y, 4, 8, PRG32_COLOR_BLUE);
    }
    snprintf(score, sizeof(score), "%02d  %02d", pong_demo.left_score % 100, pong_demo.right_score % 100);
    prg32_gfx_text8(128, 36, score, PRG32_COLOR_CYAN, 0);
    prg32_gfx_rect(20, pong_demo.left_y, 8, 42, PRG32_COLOR_WHITE);
    prg32_gfx_rect(292, pong_demo.right_y, 8, 42, PRG32_COLOR_WHITE);
    prg32_gfx_rect(pong_demo.ball_x, pong_demo.ball_y, 8, 8, PRG32_COLOR_YELLOW);
    draw_footer();
}

#define BREAKOUT_ROWS 5
#define BREAKOUT_COLS 10

typedef struct {
    int initialized;
    uint8_t bricks[BREAKOUT_ROWS][BREAKOUT_COLS];
    int ball_x;
    int ball_y;
    int ball_dx;
    int ball_dy;
    int paddle_x;
} breakout_demo_t;

static breakout_demo_t breakout_demo;

static void reset_breakout_demo(void) {
    breakout_demo.initialized = 1;
    for (int row = 0; row < BREAKOUT_ROWS; ++row) {
        for (int col = 0; col < BREAKOUT_COLS; ++col) {
            breakout_demo.bricks[row][col] = 1;
        }
    }
    breakout_demo.ball_x = 154;
    breakout_demo.ball_y = 132;
    breakout_demo.ball_dx = 3;
    breakout_demo.ball_dy = -3;
    breakout_demo.paddle_x = 124;
}

static void draw_breakout(uint32_t frame) {
    if (!breakout_demo.initialized) {
        reset_breakout_demo();
    }
    breakout_demo.paddle_x = 84 + tri_wave(frame, 96, 112);
    int old_y = breakout_demo.ball_y;
    breakout_demo.ball_x += breakout_demo.ball_dx;
    breakout_demo.ball_y += breakout_demo.ball_dy;
    if (breakout_demo.ball_x <= 8 || breakout_demo.ball_x + 8 >= 312) {
        breakout_demo.ball_dx = -breakout_demo.ball_dx;
        breakout_demo.ball_x = demo_clamp(breakout_demo.ball_x, 8, 304);
    }
    if (breakout_demo.ball_y <= DEMO_FIELD_TOP + 4) {
        breakout_demo.ball_dy = demo_abs(breakout_demo.ball_dy);
        breakout_demo.ball_y = DEMO_FIELD_TOP + 4;
    }
    if (rect_hit(breakout_demo.ball_x,
                 breakout_demo.ball_y,
                 8,
                 8,
                 breakout_demo.paddle_x,
                 170,
                 72,
                 8) &&
        old_y + 8 <= 170) {
        int hit = breakout_demo.ball_x + 4 - (breakout_demo.paddle_x + 36);
        breakout_demo.ball_dy = -demo_abs(breakout_demo.ball_dy);
        breakout_demo.ball_dx = demo_clamp(hit / 9, -4, 4);
        if (breakout_demo.ball_dx == 0) breakout_demo.ball_dx = (frame & 1u) ? 2 : -2;
        breakout_demo.ball_y = 162;
    }

    int bricks_left = 0;
    for (int row = 0; row < BREAKOUT_ROWS; ++row) {
        for (int col = 0; col < BREAKOUT_COLS; ++col) {
            if (!breakout_demo.bricks[row][col]) {
                continue;
            }
            bricks_left++;
            int bx = 14 + col * 30;
            int by = 48 + row * 12;
            if (rect_hit(breakout_demo.ball_x, breakout_demo.ball_y, 8, 8, bx, by, 26, 8)) {
                breakout_demo.bricks[row][col] = 0;
                if (old_y + 8 <= by || old_y >= by + 8) {
                    breakout_demo.ball_dy = -breakout_demo.ball_dy;
                } else {
                    breakout_demo.ball_dx = -breakout_demo.ball_dx;
                }
                bricks_left--;
                row = BREAKOUT_ROWS;
                break;
            }
        }
    }
    if (breakout_demo.ball_y > 188 || bricks_left <= 0) {
        reset_breakout_demo();
    }

    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("BREAKOUT-INSPIRED DEMO", "BRICKS DISAPPEAR ON HIT");
    for (int row = 0; row < BREAKOUT_ROWS; ++row) {
        uint16_t color = row & 1 ? PRG32_COLOR_MAGENTA : PRG32_COLOR_CYAN;
        for (int col = 0; col < BREAKOUT_COLS; ++col) {
            if (breakout_demo.bricks[row][col]) {
                prg32_gfx_rect(14 + col * 30, 48 + row * 12, 26, 8, color);
                prg32_gfx_rect(16 + col * 30, 50 + row * 12, 22, 4, PRG32_COLOR_WHITE);
            }
        }
    }
    prg32_gfx_rect(breakout_demo.paddle_x, 170, 72, 8, PRG32_COLOR_WHITE);
    prg32_gfx_rect(breakout_demo.ball_x, breakout_demo.ball_y, 8, 8, PRG32_COLOR_YELLOW);
    draw_footer();
}

#define INV_ROWS 4
#define INV_COLS 9
#define INV_ROCKETS 4

typedef struct {
    int x;
    int y;
    int active;
} rocket_t;

typedef struct {
    int initialized;
    uint8_t alive[INV_ROWS][INV_COLS];
    uint8_t shield[3][8];
    rocket_t alien_rocket[INV_ROCKETS];
    rocket_t player_rocket;
    int formation_x;
    int formation_y;
    int formation_dx;
    int player_x;
    int player_alive;
    int player_respawn;
} invaders_demo_t;

static invaders_demo_t invaders_demo;

static void reset_invaders_demo(void) {
    invaders_demo.initialized = 1;
    for (int row = 0; row < INV_ROWS; ++row) {
        for (int col = 0; col < INV_COLS; ++col) {
            invaders_demo.alive[row][col] = 1;
        }
    }
    for (int shield = 0; shield < 3; ++shield) {
        for (int cell = 0; cell < 8; ++cell) {
            invaders_demo.shield[shield][cell] = cell == 1 || cell == 6 ? 1 : 2;
        }
    }
    for (int i = 0; i < INV_ROCKETS; ++i) {
        invaders_demo.alien_rocket[i].active = 0;
    }
    invaders_demo.player_rocket.active = 0;
    invaders_demo.formation_x = 28;
    invaders_demo.formation_y = 48;
    invaders_demo.formation_dx = 2;
    invaders_demo.player_x = 146;
    invaders_demo.player_alive = 1;
    invaders_demo.player_respawn = 0;
}

static int invader_rect(int row, int col, int *x, int *y) {
    if (!invaders_demo.alive[row][col]) {
        return 0;
    }
    *x = invaders_demo.formation_x + col * 28;
    *y = invaders_demo.formation_y + row * 18;
    return 1;
}

static int shield_hit(int x, int y, int w, int h) {
    for (int shield = 0; shield < 3; ++shield) {
        int sx = 52 + shield * 88;
        int sy = 138;
        for (int cell = 0; cell < 8; ++cell) {
            if (!invaders_demo.shield[shield][cell]) {
                continue;
            }
            int cx = sx + (cell & 3) * 10;
            int cy = sy + (cell >> 2) * 8;
            if (rect_hit(x, y, w, h, cx, cy, 8, 6)) {
                invaders_demo.shield[shield][cell]--;
                return 1;
            }
        }
    }
    return 0;
}

static void draw_alien(int x, int y, uint16_t color) {
    prg32_gfx_rect(x + 2, y, 12, 4, color);
    prg32_gfx_rect(x, y + 4, 16, 6, color);
    prg32_gfx_rect(x + 3, y + 10, 3, 3, color);
    prg32_gfx_rect(x + 10, y + 10, 3, 3, color);
}

static void draw_player_ship(int x, int y, uint16_t color) {
    prg32_gfx_rect(x + 12, y, 8, 6, color);
    prg32_gfx_rect(x + 4, y + 6, 24, 6, color);
    prg32_gfx_rect(x, y + 12, 32, 4, color);
}

static void draw_space_invaders(uint32_t frame) {
    if (!invaders_demo.initialized) {
        reset_invaders_demo();
    }
    invaders_demo.player_x = 40 + tri_wave(frame, 150, 220);
    if ((frame & 3u) == 0u) {
        invaders_demo.formation_x += invaders_demo.formation_dx;
        int min_x = 320;
        int max_x = 0;
        int alive_count = 0;
        for (int row = 0; row < INV_ROWS; ++row) {
            for (int col = 0; col < INV_COLS; ++col) {
                int x;
                int y;
                if (invader_rect(row, col, &x, &y)) {
                    if (x < min_x) min_x = x;
                    if (x + 16 > max_x) max_x = x + 16;
                    alive_count++;
                }
            }
        }
        if (alive_count == 0) {
            reset_invaders_demo();
        } else if (min_x <= 12 || max_x >= 308) {
            invaders_demo.formation_dx = -invaders_demo.formation_dx;
            invaders_demo.formation_y += 4;
        }
    }

    if (!invaders_demo.player_rocket.active && invaders_demo.player_alive && (frame % 24u) == 0u) {
        invaders_demo.player_rocket.active = 1;
        invaders_demo.player_rocket.x = invaders_demo.player_x + 15;
        invaders_demo.player_rocket.y = 156;
    }
    if (invaders_demo.player_rocket.active) {
        invaders_demo.player_rocket.y -= 5;
        if (shield_hit(invaders_demo.player_rocket.x, invaders_demo.player_rocket.y, 2, 8) ||
            invaders_demo.player_rocket.y < DEMO_FIELD_TOP) {
            invaders_demo.player_rocket.active = 0;
        }
        for (int row = 0; row < INV_ROWS && invaders_demo.player_rocket.active; ++row) {
            for (int col = 0; col < INV_COLS; ++col) {
                int x;
                int y;
                if (invader_rect(row, col, &x, &y) &&
                    rect_hit(invaders_demo.player_rocket.x,
                             invaders_demo.player_rocket.y,
                             2,
                             8,
                             x,
                             y,
                             16,
                             13)) {
                    invaders_demo.alive[row][col] = 0;
                    invaders_demo.player_rocket.active = 0;
                    break;
                }
            }
        }
    }

    if ((frame % 18u) == 0u) {
        int col = (int)((frame / 18u) % INV_COLS);
        for (int slot = 0; slot < INV_ROCKETS; ++slot) {
            if (invaders_demo.alien_rocket[slot].active) {
                continue;
            }
            for (int row = INV_ROWS - 1; row >= 0; --row) {
                int x;
                int y;
                if (invader_rect(row, col, &x, &y)) {
                    invaders_demo.alien_rocket[slot].active = 1;
                    invaders_demo.alien_rocket[slot].x = x + 7;
                    invaders_demo.alien_rocket[slot].y = y + 14;
                    row = -1;
                }
            }
            break;
        }
    }

    for (int i = 0; i < INV_ROCKETS; ++i) {
        rocket_t *rocket = &invaders_demo.alien_rocket[i];
        if (!rocket->active) {
            continue;
        }
        rocket->y += 3;
        if (shield_hit(rocket->x, rocket->y, 2, 8) || rocket->y > 190) {
            rocket->active = 0;
            continue;
        }
        if (invaders_demo.player_alive &&
            rect_hit(rocket->x, rocket->y, 2, 8, invaders_demo.player_x, 160, 32, 16)) {
            invaders_demo.player_alive = 0;
            invaders_demo.player_respawn = 36;
            rocket->active = 0;
        }
    }
    if (!invaders_demo.player_alive && invaders_demo.player_respawn-- <= 0) {
        invaders_demo.player_alive = 1;
    }
    if (invaders_demo.formation_y > 90) {
        reset_invaders_demo();
    }

    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("SPACE INVADERS DEMO", "ALIEN ROCKETS, SHIELDS, HITS");
    for (int row = 0; row < INV_ROWS; ++row) {
        for (int col = 0; col < INV_COLS; ++col) {
            int x;
            int y;
            if (invader_rect(row, col, &x, &y)) {
                draw_alien(x, y, row & 1 ? PRG32_COLOR_GREEN : PRG32_COLOR_CYAN);
            }
        }
    }
    for (int shield = 0; shield < 3; ++shield) {
        int sx = 52 + shield * 88;
        for (int cell = 0; cell < 8; ++cell) {
            if (invaders_demo.shield[shield][cell]) {
                uint16_t color = invaders_demo.shield[shield][cell] > 1
                    ? PRG32_COLOR_GREEN
                    : PRG32_COLOR_YELLOW;
                prg32_gfx_rect(sx + (cell & 3) * 10, 138 + (cell >> 2) * 8, 8, 6, color);
            }
        }
    }
    if (invaders_demo.player_rocket.active) {
        prg32_gfx_rect(invaders_demo.player_rocket.x,
                       invaders_demo.player_rocket.y,
                       2,
                       8,
                       PRG32_COLOR_WHITE);
    }
    for (int i = 0; i < INV_ROCKETS; ++i) {
        if (invaders_demo.alien_rocket[i].active) {
            prg32_gfx_rect(invaders_demo.alien_rocket[i].x,
                           invaders_demo.alien_rocket[i].y,
                           2,
                           8,
                           PRG32_COLOR_RED);
        }
    }
    if (invaders_demo.player_alive) {
        draw_player_ship(invaders_demo.player_x, 160, PRG32_COLOR_CYAN);
    } else {
        prg32_gfx_rect(invaders_demo.player_x + 8, 160, 16, 16, PRG32_COLOR_RED);
        prg32_gfx_rect(invaders_demo.player_x + 2, 166, 28, 4, PRG32_COLOR_YELLOW);
    }
    draw_footer();
}

#define PAC_ROWS 11
#define PAC_COLS 21
#define PAC_CELL 12

static const char pac_maze[PAC_ROWS][PAC_COLS + 1] = {
    "#####################",
    "#.........#.........#",
    "#.###.###.#.###.###.#",
    "#o#.....#...#.....#o#",
    "#.###.#.#####.#.###.#",
    "#.....#...#...#.....#",
    "#####.### # ###.#####",
    "#.........P.........#",
    "#.###.#.#####.#.###.#",
    "#o....#.......#....o#",
    "#####################",
};

typedef struct {
    int initialized;
    int full_redraw;
    uint8_t dots[PAC_ROWS][PAC_COLS];
    uint8_t power[PAC_ROWS][PAC_COLS];
    int path_index;
    int super_timer;
} pac_demo_t;

typedef struct {
    uint8_t col;
    uint8_t row;
} pac_cell_t;

static const pac_cell_t pac_path[] = {
    {1,1},{2,1},{3,1},{4,1},{5,1},{6,1},{7,1},{8,1},{9,1},
    {9,3},{10,3},{11,3},{12,3},{13,3},{14,3},{15,3},{16,3},{17,3},{18,3},{19,3},
    {19,5},{18,5},{17,5},{16,5},{15,5},{14,5},{13,5},{12,5},{11,5},{10,5},{9,5},
    {8,5},{7,5},{6,5},{5,5},{4,5},{3,5},{2,5},{1,5},
    {1,7},{2,7},{3,7},{4,7},{5,7},{6,7},{7,7},{8,7},{9,7},{10,7},{11,7},{12,7},
    {13,7},{14,7},{15,7},{16,7},{17,7},{18,7},{19,7},
    {19,9},{18,9},{17,9},{16,9},{15,9},{14,9},{13,9},{12,9},{11,9},{10,9},
    {9,9},{8,9},{7,9},{6,9},{5,9},{4,9},{3,9},{2,9},{1,9},
};

static pac_demo_t pac_demo;

static void reset_pac_demo(void) {
    pac_demo.initialized = 1;
    pac_demo.full_redraw = 1;
    pac_demo.path_index = 0;
    pac_demo.super_timer = 0;
    for (int row = 0; row < PAC_ROWS; ++row) {
        for (int col = 0; col < PAC_COLS; ++col) {
            pac_demo.dots[row][col] = pac_maze[row][col] == '.';
            pac_demo.power[row][col] = pac_maze[row][col] == 'o';
        }
    }
}

static void draw_ghost(int x, int y, uint16_t color) {
    prg32_gfx_rect(x + 2, y, 12, 4, color);
    prg32_gfx_rect(x, y + 4, 16, 12, color);
    prg32_gfx_rect(x + 3, y + 7, 3, 3, PRG32_COLOR_WHITE);
    prg32_gfx_rect(x + 10, y + 7, 3, 3, PRG32_COLOR_WHITE);
    prg32_gfx_rect(x, y + 16, 4, 3, color);
    prg32_gfx_rect(x + 6, y + 16, 4, 3, color);
    prg32_gfx_rect(x + 12, y + 16, 4, 3, color);
}

static void draw_pacman(uint32_t frame) {
    if (!pac_demo.initialized) {
        reset_pac_demo();
    }
    if ((frame & 3u) == 0u) {
        pac_demo.path_index = (pac_demo.path_index + 1) %
            (int)(sizeof(pac_path) / sizeof(pac_path[0]));
    }
    pac_cell_t cell = pac_path[pac_demo.path_index];
    if (pac_demo.dots[cell.row][cell.col]) {
        pac_demo.dots[cell.row][cell.col] = 0;
    }
    if (pac_demo.power[cell.row][cell.col]) {
        pac_demo.power[cell.row][cell.col] = 0;
        pac_demo.super_timer = 150;
    }
    if (pac_demo.super_timer > 0) {
        pac_demo.super_timer--;
    }

    int x0 = 34;
    int y0 = 44;
    if (pac_demo.full_redraw) {
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        draw_title("PACMAN-INSPIRED DEMO", "DOTS, POWER DOTS, MOVING GHOSTS");
        draw_footer();
        pac_demo.full_redraw = 0;
    } else {
        prg32_gfx_rect(x0, y0, PAC_COLS * PAC_CELL, PAC_ROWS * PAC_CELL, PRG32_COLOR_BLACK);
    }
    for (int row = 0; row < PAC_ROWS; ++row) {
        for (int col = 0; col < PAC_COLS; ++col) {
            int x = x0 + col * PAC_CELL;
            int y = y0 + row * PAC_CELL;
            if (pac_maze[row][col] == '#') {
                prg32_gfx_rect(x, y, PAC_CELL - 1, PAC_CELL - 1, PRG32_COLOR_BLUE);
            } else if (pac_demo.power[row][col]) {
                prg32_gfx_rect(x + 4, y + 4, 5, 5, PRG32_COLOR_RED);
            } else if (pac_demo.dots[row][col]) {
                prg32_gfx_rect(x + 5, y + 5, 3, 3, PRG32_COLOR_YELLOW);
            }
        }
    }

    int px = x0 + cell.col * PAC_CELL + 1;
    int py = y0 + cell.row * PAC_CELL + 1;
    int mouth = (frame / 5u) & 1u;
    uint16_t pac_color = pac_demo.super_timer > 0 ? PRG32_COLOR_WHITE : PRG32_COLOR_YELLOW;
    prg32_gfx_rect(px, py, 10, 10, pac_color);
    if (mouth) {
        prg32_gfx_rect(px + 7, py + 3, 4, 4, PRG32_COLOR_BLACK);
    }
    for (int i = 0; i < 4; ++i) {
        int idx = (pac_demo.path_index + 12 + i * 17) %
            (int)(sizeof(pac_path) / sizeof(pac_path[0]));
        pac_cell_t ghost = pac_path[idx];
        uint16_t color = pac_demo.super_timer > 0
            ? PRG32_COLOR_CYAN
            : (i == 0 ? PRG32_COLOR_RED :
               i == 1 ? PRG32_COLOR_MAGENTA :
               i == 2 ? PRG32_COLOR_GREEN : PRG32_COLOR_CYAN);
        draw_ghost(x0 + ghost.col * PAC_CELL - 2,
                   y0 + ghost.row * PAC_CELL - 4,
                   color);
    }
}

#define TETRIS_W 10
#define TETRIS_H 14
#define TETRIS_BLOCK 9

typedef struct {
    int initialized;
    uint8_t board[TETRIS_H][TETRIS_W];
    int piece;
    int rot;
    int x;
    int y;
    int fall_px;
    int next[3];
} tetris_demo_t;

static const uint16_t tetris_colors[] = {
    PRG32_COLOR_BLACK,
    PRG32_COLOR_CYAN,
    PRG32_COLOR_BLUE,
    PRG32_COLOR_YELLOW,
    PRG32_COLOR_GREEN,
    PRG32_COLOR_MAGENTA,
    PRG32_COLOR_RED,
    PRG32_COLOR_WHITE,
};

static const int8_t tetris_shape[7][4][4][2] = {
    {{{0,1},{1,1},{2,1},{3,1}},{{2,0},{2,1},{2,2},{2,3}},{{0,2},{1,2},{2,2},{3,2}},{{1,0},{1,1},{1,2},{1,3}}},
    {{{0,0},{0,1},{1,1},{2,1}},{{1,0},{2,0},{1,1},{1,2}},{{0,1},{1,1},{2,1},{2,2}},{{1,0},{1,1},{0,2},{1,2}}},
    {{{2,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{1,2},{2,2}},{{0,1},{1,1},{2,1},{0,2}},{{0,0},{1,0},{1,1},{1,2}}},
    {{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}}},
    {{{1,0},{2,0},{0,1},{1,1}},{{1,0},{1,1},{2,1},{2,2}},{{1,1},{2,1},{0,2},{1,2}},{{0,0},{0,1},{1,1},{1,2}}},
    {{{1,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{2,1},{1,2}},{{1,0},{0,1},{1,1},{1,2}}},
    {{{0,0},{1,0},{1,1},{2,1}},{{2,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{1,2},{2,2}},{{1,0},{0,1},{1,1},{0,2}}},
};

static tetris_demo_t tetris_demo;

static int tetris_fits(int piece, int rot, int x, int y) {
    for (int i = 0; i < 4; ++i) {
        int px = x + tetris_shape[piece][rot][i][0];
        int py = y + tetris_shape[piece][rot][i][1];
        if (px < 0 || px >= TETRIS_W || py >= TETRIS_H) {
            return 0;
        }
        if (py >= 0 && tetris_demo.board[py][px]) {
            return 0;
        }
    }
    return 1;
}

static void tetris_spawn(void) {
    tetris_demo.piece = tetris_demo.next[0];
    tetris_demo.next[0] = tetris_demo.next[1];
    tetris_demo.next[1] = tetris_demo.next[2];
    tetris_demo.next[2] = (tetris_demo.next[2] + 3) % 7;
    tetris_demo.rot = 0;
    tetris_demo.x = 3;
    tetris_demo.y = -1;
    tetris_demo.fall_px = 0;
    if (!tetris_fits(tetris_demo.piece, tetris_demo.rot, tetris_demo.x, tetris_demo.y)) {
        for (int row = 0; row < TETRIS_H; ++row) {
            for (int col = 0; col < TETRIS_W; ++col) {
                tetris_demo.board[row][col] = 0;
            }
        }
    }
}

static void reset_tetris_demo(void) {
    tetris_demo.initialized = 1;
    for (int row = 0; row < TETRIS_H; ++row) {
        for (int col = 0; col < TETRIS_W; ++col) {
            tetris_demo.board[row][col] = row > 10 && ((row + col) % 5) == 0
                ? (uint8_t)((col % 7) + 1)
                : 0;
        }
    }
    tetris_demo.next[0] = 0;
    tetris_demo.next[1] = 3;
    tetris_demo.next[2] = 5;
    tetris_spawn();
}

static void tetris_lock_piece(void) {
    for (int i = 0; i < 4; ++i) {
        int px = tetris_demo.x + tetris_shape[tetris_demo.piece][tetris_demo.rot][i][0];
        int py = tetris_demo.y + tetris_shape[tetris_demo.piece][tetris_demo.rot][i][1];
        if (py >= 0 && py < TETRIS_H && px >= 0 && px < TETRIS_W) {
            tetris_demo.board[py][px] = (uint8_t)(tetris_demo.piece + 1);
        }
    }
    for (int row = TETRIS_H - 1; row >= 0; --row) {
        int full = 1;
        for (int col = 0; col < TETRIS_W; ++col) {
            if (!tetris_demo.board[row][col]) {
                full = 0;
                break;
            }
        }
        if (full) {
            for (int y = row; y > 0; --y) {
                for (int col = 0; col < TETRIS_W; ++col) {
                    tetris_demo.board[y][col] = tetris_demo.board[y - 1][col];
                }
            }
            for (int col = 0; col < TETRIS_W; ++col) {
                tetris_demo.board[0][col] = 0;
            }
            row++;
        }
    }
    tetris_spawn();
}

static void draw_tetris_block(int x, int y, uint16_t color) {
    prg32_gfx_rect(x, y, TETRIS_BLOCK - 1, TETRIS_BLOCK - 1, color);
    prg32_gfx_rect(x + 2, y + 2, TETRIS_BLOCK - 5, 2, PRG32_COLOR_WHITE);
}

static void draw_tetris(uint32_t frame) {
    if (!tetris_demo.initialized) {
        reset_tetris_demo();
    }
    if ((frame % 24u) == 0u) {
        int next_rot = (tetris_demo.rot + 1) & 3;
        if (tetris_fits(tetris_demo.piece, next_rot, tetris_demo.x, tetris_demo.y)) {
            tetris_demo.rot = next_rot;
        }
    }
    if ((frame % 32u) == 0u) {
        int dx = ((frame / 32u) & 1u) ? 1 : -1;
        if (tetris_fits(tetris_demo.piece, tetris_demo.rot, tetris_demo.x + dx, tetris_demo.y)) {
            tetris_demo.x += dx;
        }
    }
    tetris_demo.fall_px += 2;
    if (tetris_demo.fall_px >= TETRIS_BLOCK) {
        tetris_demo.fall_px -= TETRIS_BLOCK;
        if (tetris_fits(tetris_demo.piece, tetris_demo.rot, tetris_demo.x, tetris_demo.y + 1)) {
            tetris_demo.y++;
        } else {
            tetris_lock_piece();
        }
    }

    int bx = 102;
    int by = 42;
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("TETRIS-INSPIRED DEMO", "NEXT PIECES, ROTATION, GRAVITY");
    prg32_gfx_rect(bx - 4, by - 4, TETRIS_W * TETRIS_BLOCK + 8, TETRIS_H * TETRIS_BLOCK + 8, PRG32_COLOR_BLUE);
    prg32_gfx_rect(bx, by, TETRIS_W * TETRIS_BLOCK, TETRIS_H * TETRIS_BLOCK, PRG32_COLOR_BLACK);
    for (int row = 0; row < TETRIS_H; ++row) {
        for (int col = 0; col < TETRIS_W; ++col) {
            if (tetris_demo.board[row][col]) {
                draw_tetris_block(bx + col * TETRIS_BLOCK,
                                  by + row * TETRIS_BLOCK,
                                  tetris_colors[tetris_demo.board[row][col]]);
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        int px = tetris_demo.x + tetris_shape[tetris_demo.piece][tetris_demo.rot][i][0];
        int py = tetris_demo.y + tetris_shape[tetris_demo.piece][tetris_demo.rot][i][1];
        if (py >= 0) {
            draw_tetris_block(bx + px * TETRIS_BLOCK,
                              by + py * TETRIS_BLOCK + tetris_demo.fall_px,
                              tetris_colors[tetris_demo.piece + 1]);
        }
    }
    prg32_gfx_text8(220, 48, "NEXT", PRG32_COLOR_CYAN, 0);
    for (int n = 0; n < 3; ++n) {
        int piece = tetris_demo.next[n];
        for (int i = 0; i < 4; ++i) {
            int px = tetris_shape[piece][0][i][0];
            int py = tetris_shape[piece][0][i][1];
            draw_tetris_block(224 + px * 8,
                              66 + n * 34 + py * 8,
                              tetris_colors[piece + 1]);
        }
    }
    draw_footer();
}

static void draw_cloud(int x, int y) {
    prg32_gfx_rect(x, y + 6, 30, 6, PRG32_COLOR_WHITE);
    prg32_gfx_rect(x + 6, y + 2, 10, 8, PRG32_COLOR_WHITE);
    prg32_gfx_rect(x + 17, y, 12, 10, PRG32_COLOR_WHITE);
}

static void draw_car_sprite(int x, int y) {
    prg32_gfx_rect(x + 7, y, 20, 8, PRG32_COLOR_RED);
    prg32_gfx_rect(x + 2, y + 8, 30, 10, PRG32_COLOR_RED);
    prg32_gfx_rect(x + 10, y + 2, 14, 6, PRG32_COLOR_CYAN);
    prg32_gfx_rect(x, y + 16, 8, 6, PRG32_COLOR_BLACK);
    prg32_gfx_rect(x + 26, y + 16, 8, 6, PRG32_COLOR_BLACK);
    prg32_gfx_rect(x + 3, y + 17, 4, 3, PRG32_COLOR_WHITE);
    prg32_gfx_rect(x + 27, y + 17, 4, 3, PRG32_COLOR_WHITE);
}

static void draw_pole_position(uint32_t frame) {
    prg32_gfx_clear(PRG32_COLOR_CYAN);
    draw_title("POLE POSITION DEMO", "CLOUD PLAYFIELD + CURVED ROAD");
    for (int i = 0; i < 4; ++i) {
        int x = (int)((frame / 2u + (uint32_t)i * 82u) % 380u) - 50;
        draw_cloud(x, 54 + (i & 1) * 16);
    }
    prg32_gfx_rect(0, 92, PRG32_GAME_W, 108, PRG32_COLOR_GREEN);
    int curve = tri_wave(frame, 180, 80) - 40;
    for (int y = 94; y < 200; y += 2) {
        int t = y - 94;
        int center = 160 + (curve * t * t) / (106 * 106);
        int road_w = 36 + t * 2;
        int left = center - road_w / 2;
        int right = center + road_w / 2;
        prg32_gfx_rect(left, y, right - left, 2, 0x7bef);
        prg32_gfx_rect(left - 4, y, 4, 2, (y / 8) & 1 ? PRG32_COLOR_RED : PRG32_COLOR_WHITE);
        prg32_gfx_rect(right, y, 4, 2, (y / 8) & 1 ? PRG32_COLOR_RED : PRG32_COLOR_WHITE);
        if (((y + (int)(frame * 2u)) / 14) & 1) {
            int lane_w = 3 + t / 18;
            prg32_gfx_rect(center - lane_w / 2, y, lane_w, 2, PRG32_COLOR_YELLOW);
        }
    }
    int car_x = 142 + curve / 5;
    draw_car_sprite(car_x, 160);
    draw_footer();
}

#define AST_COUNT 6
#define BULLET_COUNT 3

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int size;
    int alive;
} asteroid_t;

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int life;
} bullet_t;

typedef struct {
    int initialized;
    asteroid_t asteroids[AST_COUNT];
    bullet_t bullets[BULLET_COUNT];
    int ship_angle;
} asteroids_demo_t;

static asteroids_demo_t asteroids_demo;

static void reset_asteroids_demo(void) {
    asteroids_demo.initialized = 1;
    asteroids_demo.ship_angle = 0;
    for (int i = 0; i < AST_COUNT; ++i) {
        asteroids_demo.asteroids[i].x = 22 + i * 47;
        asteroids_demo.asteroids[i].y = 52 + (i * 31) % 112;
        asteroids_demo.asteroids[i].vx = (i & 1) ? 2 : -2;
        asteroids_demo.asteroids[i].vy = (i & 2) ? 1 : -1;
        asteroids_demo.asteroids[i].size = 8 + (i % 3) * 4;
        asteroids_demo.asteroids[i].alive = 1;
    }
    for (int i = 0; i < BULLET_COUNT; ++i) {
        asteroids_demo.bullets[i].life = 0;
    }
}

static void wrap_point(int *x, int *y) {
    if (*x < 0) *x += PRG32_GAME_W;
    if (*x >= PRG32_GAME_W) *x -= PRG32_GAME_W;
    if (*y < DEMO_FIELD_TOP) *y = DEMO_FIELD_BOTTOM - 1;
    if (*y >= DEMO_FIELD_BOTTOM) *y = DEMO_FIELD_TOP;
}

static void draw_asteroid_shape(int x, int y, int s, uint16_t color) {
    draw_line(x - s, y - 2, x - s / 2, y - s, color);
    draw_line(x - s / 2, y - s, x + s / 2, y - s + 2, color);
    draw_line(x + s / 2, y - s + 2, x + s, y - 1, color);
    draw_line(x + s, y - 1, x + s / 2, y + s, color);
    draw_line(x + s / 2, y + s, x - s / 2, y + s - 1, color);
    draw_line(x - s / 2, y + s - 1, x - s, y - 2, color);
}

static void draw_asteroids(uint32_t frame) {
    static const int dirs[8][2] = {
        {0,-12},{8,-8},{12,0},{8,8},{0,12},{-8,8},{-12,0},{-8,-8},
    };
    if (!asteroids_demo.initialized) {
        reset_asteroids_demo();
    }
    asteroids_demo.ship_angle = (asteroids_demo.ship_angle + 1) & 7;
    int sx = 160;
    int sy = 112;
    int dx = dirs[asteroids_demo.ship_angle][0];
    int dy = dirs[asteroids_demo.ship_angle][1];
    if ((frame % 22u) == 0u) {
        for (int i = 0; i < BULLET_COUNT; ++i) {
            if (asteroids_demo.bullets[i].life <= 0) {
                asteroids_demo.bullets[i].x = sx + dx;
                asteroids_demo.bullets[i].y = sy + dy;
                asteroids_demo.bullets[i].vx = dx / 2;
                asteroids_demo.bullets[i].vy = dy / 2;
                asteroids_demo.bullets[i].life = 34;
                break;
            }
        }
    }
    for (int i = 0; i < AST_COUNT; ++i) {
        asteroid_t *asteroid = &asteroids_demo.asteroids[i];
        if (!asteroid->alive) {
            if ((frame % 90u) == (uint32_t)(i * 7)) {
                asteroid->alive = 1;
                asteroid->x = 24 + i * 43;
                asteroid->y = DEMO_FIELD_TOP + 12;
                asteroid->size = 8 + (i % 3) * 4;
            }
            continue;
        }
        asteroid->x += asteroid->vx;
        asteroid->y += asteroid->vy;
        wrap_point(&asteroid->x, &asteroid->y);
    }
    for (int b = 0; b < BULLET_COUNT; ++b) {
        bullet_t *bullet = &asteroids_demo.bullets[b];
        if (bullet->life <= 0) {
            continue;
        }
        bullet->x += bullet->vx;
        bullet->y += bullet->vy;
        wrap_point(&bullet->x, &bullet->y);
        bullet->life--;
        for (int i = 0; i < AST_COUNT; ++i) {
            asteroid_t *asteroid = &asteroids_demo.asteroids[i];
            if (asteroid->alive &&
                demo_abs(bullet->x - asteroid->x) < asteroid->size &&
                demo_abs(bullet->y - asteroid->y) < asteroid->size) {
                bullet->life = 0;
                asteroid->size -= 4;
                asteroid->vx = -asteroid->vx;
                if (asteroid->size < 6) {
                    asteroid->alive = 0;
                }
                break;
            }
        }
    }

    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("ASTEROIDS-INSPIRED DEMO", "WRAP, ROTATE, SHOOT, SPLIT");
    for (int i = 0; i < 36; ++i) {
        int x = (i * 53 + (int)frame) % PRG32_GAME_W;
        int y = DEMO_FIELD_TOP + (i * 29) % (DEMO_FIELD_BOTTOM - DEMO_FIELD_TOP);
        prg32_gfx_pixel(x, y, PRG32_COLOR_WHITE);
    }
    draw_line(sx + dx, sy + dy, sx - dy / 2, sy + dx / 2, PRG32_COLOR_CYAN);
    draw_line(sx + dx, sy + dy, sx + dy / 2, sy - dx / 2, PRG32_COLOR_CYAN);
    draw_line(sx - dy / 2, sy + dx / 2, sx + dy / 2, sy - dx / 2, PRG32_COLOR_CYAN);
    for (int i = 0; i < AST_COUNT; ++i) {
        if (asteroids_demo.asteroids[i].alive) {
            draw_asteroid_shape(asteroids_demo.asteroids[i].x,
                                asteroids_demo.asteroids[i].y,
                                asteroids_demo.asteroids[i].size,
                                PRG32_COLOR_WHITE);
        }
    }
    for (int i = 0; i < BULLET_COUNT; ++i) {
        if (asteroids_demo.bullets[i].life > 0) {
            prg32_gfx_rect(asteroids_demo.bullets[i].x,
                           asteroids_demo.bullets[i].y,
                           2,
                           2,
                           PRG32_COLOR_YELLOW);
        }
    }
    draw_footer();
}

static void reset_demo_page(int page) {
    if (page == 3) reset_pong_demo();
    if (page == 4) reset_breakout_demo();
    if (page == 5) reset_invaders_demo();
    if (page == 6) reset_pac_demo();
    if (page == 7) reset_tetris_demo();
    if (page == 9) reset_asteroids_demo();
}

void prg32_device_demo_run(void) {
    int was_fullscreen = prg32_gfx_fullscreen_enabled();
    prg32_band_mode_t top_mode = prg32_band_mode(PRG32_BAND_TOP);
    prg32_band_mode_t bottom_mode = prg32_band_mode(PRG32_BAND_BOTTOM);

    prg32_gfx_set_fullscreen(0);
    prg32_band_set_mode(PRG32_BAND_TOP, PRG32_BAND_MODE_FPS);
    prg32_band_set_mode(PRG32_BAND_BOTTOM, PRG32_BAND_MODE_CUSTOM);
    prg32_band_set_game_info("PRG32 DEVICE DEMO");
    demo_prepare_playfields();
    prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT);

    int page = 0;
    uint32_t frame = 0;
    uint32_t last = 0;
    uint32_t next_frame_ms = prg32_ticks_ms();
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_audio_beep(330, 50);
            prg32_input_wait_released(PRG32_BTN_A);
            break;
        }
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            page = (page + 1) % DEMO_PAGE_COUNT;
            reset_demo_page(page);
            prg32_audio_beep(880, 40);
            prg32_input_wait_released(PRG32_BTN_B | PRG32_BTN_SELECT);
            next_frame_ms = prg32_ticks_ms();
        }

        switch (page) {
            case 0: draw_overview(frame); break;
            case 1: draw_graphics(frame); break;
            case 2: draw_system(frame); break;
            case 3: draw_pong(frame); break;
            case 4: draw_breakout(frame); break;
            case 5: draw_space_invaders(frame); break;
            case 6: draw_pacman(frame); break;
            case 7: draw_tetris(frame); break;
            case 8: draw_pole_position(frame); break;
            default: draw_asteroids(frame); break;
        }

        char band[56];
        snprintf(band,
                 sizeof(band),
                 "PAGE %d/%d  INPUT 0x%04lx",
                 page + 1,
                 DEMO_PAGE_COUNT,
                 (unsigned long)input);
        prg32_band_set_text(PRG32_BAND_BOTTOM, band);
        prg32_gfx_present();
        last = input;
        frame++;
        wait_for_frame_target(&next_frame_ms);
    }

    prg32_band_set_game_info("");
    prg32_band_set_mode(PRG32_BAND_TOP, top_mode);
    prg32_band_set_mode(PRG32_BAND_BOTTOM, bottom_mode);
    prg32_tile_clear(PRG32_COLOR_BLACK);
    prg32_playfield_clear(0, 0);
    prg32_playfield_clear(1, 0);
    prg32_gfx_set_fullscreen(was_fullscreen);
}
