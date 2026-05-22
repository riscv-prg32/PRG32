#include "prg32.h"

#define TILE_STAR 1
#define TILE_PANEL 2
#define TILE_EDGE 3

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    int alive;
} enemy_t;

static const uint8_t tile_star[8] = {
    0x00, 0x10, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t tile_panel[8] = {
    0xff, 0xbd, 0x81, 0xa5, 0x81, 0xbd, 0x81, 0xff,
};

static const uint8_t tile_edge[8] = {
    0x18, 0x3c, 0x7e, 0xff, 0xff, 0x7e, 0x3c, 0x18,
};

static int cross_x;
static int cross_y;
static int score;
static int shield;
static int laser_timer;
static uint32_t frame_count;
static enemy_t enemies[4];

static int clamp_int(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static void setup_starfield(void) {
    prg32_playfield_clear(0, 0);
    for (uint8_t i = 0; i < 92; ++i) {
        uint8_t x = (uint8_t)((i * 17u + 5u) % PRG32_PLAYFIELD_COLS);
        uint8_t y = (uint8_t)((i * 23u + 3u) % PRG32_PLAYFIELD_ROWS);
        prg32_playfield_put(0, x, y, TILE_STAR);
    }
}

static void setup_cockpit(void) {
    prg32_playfield_clear(1, 0);
    for (uint8_t x = 0; x < PRG32_PLAYFIELD_COLS; ++x) {
        if (x < 7 || x > 32) {
            prg32_playfield_put(1, x, 7, TILE_EDGE);
        }
        for (uint8_t y = 22; y < PRG32_PLAYFIELD_ROWS; ++y) {
            prg32_playfield_put(1, x, y, TILE_PANEL);
        }
    }
    for (uint8_t y = 8; y < 22; ++y) {
        for (uint8_t x = 0; x < 4; ++x) {
            prg32_playfield_put(1, x, y, TILE_PANEL);
            prg32_playfield_put(1, 39 - x, y, TILE_PANEL);
        }
    }
    prg32_playfield_put(1, 4, 8, TILE_EDGE);
    prg32_playfield_put(1, 35, 8, TILE_EDGE);
}

static void spawn_enemy(int i) {
    enemies[i].x = 58 + i * 56;
    enemies[i].y = 46 + (i * 23) % 64;
    enemies[i].dx = (i & 1) ? 2 : -2;
    enemies[i].dy = (i & 2) ? 1 : -1;
    enemies[i].alive = 1;
}

static void draw_ship(int x, int y, uint16_t color) {
    prg32_gfx_rect(x + 14, y, 8, 8, color);
    prg32_gfx_rect(x + 8, y + 8, 20, 6, color);
    prg32_gfx_rect(x, y + 14, 36, 5, color);
    prg32_gfx_rect(x + 14, y + 7, 8, 5, PRG32_COLOR_RED);
}

static void draw_crosshair(void) {
    prg32_gfx_rect(cross_x - 14, cross_y, 10, 2, PRG32_COLOR_GREEN);
    prg32_gfx_rect(cross_x + 4, cross_y, 10, 2, PRG32_COLOR_GREEN);
    prg32_gfx_rect(cross_x, cross_y - 14, 2, 10, PRG32_COLOR_GREEN);
    prg32_gfx_rect(cross_x, cross_y + 4, 2, 10, PRG32_COLOR_GREEN);
}

static void draw_label_uint3(int x, int y, const char *label, int value, uint16_t color) {
    char digits[4];
    value %= 1000;
    if (value < 0) {
        value = 0;
    }
    digits[0] = (char)('0' + value / 100);
    digits[1] = (char)('0' + (value / 10) % 10);
    digits[2] = (char)('0' + value % 10);
    digits[3] = 0;
    prg32_gfx_text8(x, y, label, color, PRG32_COLOR_BLACK);
    prg32_gfx_text8(x + 56, y, digits, color, PRG32_COLOR_BLACK);
}

void wing_commander_c_init(void) {
    prg32_tile_define(0, NULL, PRG32_COLOR_BLACK, PRG32_COLOR_BLACK);
    prg32_tile_define(TILE_STAR, tile_star, PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
    prg32_tile_define(TILE_PANEL, tile_panel, 0x7bef, 0x2104);
    prg32_tile_define(TILE_EDGE, tile_edge, PRG32_COLOR_CYAN, 0x2104);
    prg32_playfield_parallax(0, PRG32_PARALLAX_1X, PRG32_PARALLAX_1X);
    prg32_playfield_parallax(1, 0, 0);
    setup_starfield();
    setup_cockpit();
    cross_x = 160;
    cross_y = 94;
    score = 0;
    shield = 100;
    laser_timer = 0;
    frame_count = 0;
    for (int i = 0; i < 4; ++i) {
        spawn_enemy(i);
    }
}

void wing_commander_c_update(void) {
    uint32_t input = prg32_input_read();
    if (input & PRG32_BTN_LEFT) {
        cross_x -= 4;
    }
    if (input & PRG32_BTN_RIGHT) {
        cross_x += 4;
    }
    if (input & PRG32_BTN_UP) {
        cross_y -= 3;
    }
    if (input & PRG32_BTN_DOWN) {
        cross_y += 3;
    }
    cross_x = clamp_int(cross_x, 42, 278);
    cross_y = clamp_int(cross_y, 44, 150);

    if ((input & (PRG32_BTN_A | PRG32_BTN_B)) && laser_timer == 0) {
        laser_timer = 6;
        prg32_audio_beep(880, 25);
        for (int i = 0; i < 4; ++i) {
            if (enemies[i].alive &&
                cross_x >= enemies[i].x - 8 &&
                cross_x <= enemies[i].x + 44 &&
                cross_y >= enemies[i].y - 8 &&
                cross_y <= enemies[i].y + 28) {
                score += 10;
                enemies[i].alive = 0;
                prg32_audio_beep(1320, 35);
            }
        }
    }
    if (laser_timer > 0) {
        laser_timer--;
    }

    for (int i = 0; i < 4; ++i) {
        if (!enemies[i].alive) {
            if ((frame_count % 90u) == (uint32_t)(i * 13)) {
                spawn_enemy(i);
            }
            continue;
        }
        enemies[i].x += enemies[i].dx;
        enemies[i].y += enemies[i].dy;
        if (enemies[i].x < 42 || enemies[i].x > 260) {
            enemies[i].dx = -enemies[i].dx;
        }
        if (enemies[i].y < 44 || enemies[i].y > 138) {
            enemies[i].dy = -enemies[i].dy;
        }
        if ((frame_count % 120u) == (uint32_t)(i * 17)) {
            shield -= shield > 0 ? 1 : 0;
        }
    }

    prg32_playfield_scroll(0, 0, -(int)(frame_count * 3u));
    prg32_playfield_scroll(1, 0, 0);
    frame_count++;
}

void wing_commander_c_draw(void) {
    prg32_playfield_draw_dual();
    for (int i = 0; i < 4; ++i) {
        if (enemies[i].alive) {
            draw_ship(enemies[i].x, enemies[i].y, PRG32_COLOR_MAGENTA);
        }
    }
    if (laser_timer > 0) {
        prg32_gfx_rect(158, cross_y, 3, 164 - cross_y, PRG32_COLOR_YELLOW);
        prg32_gfx_rect(cross_x, cross_y, 4, 4, PRG32_COLOR_YELLOW);
    }
    draw_crosshair();
    prg32_gfx_text8(8, 8, "WING COMMANDER C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
    draw_label_uint3(24, 176, "SCORE", score, PRG32_COLOR_YELLOW);
    draw_label_uint3(188, 176, "SHIELD", shield, PRG32_COLOR_CYAN);
}
