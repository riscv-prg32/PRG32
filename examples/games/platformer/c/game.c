#include "prg32.h"

static prg32_platform_actor_t player;
static uint32_t anim_frame;
static uint16_t coins;
static uint16_t lives;
static uint16_t win_timer;

static const uint8_t tile_empty[8] = {0};
static const uint8_t tile_cloud[8] = {
    0x00, 0x18, 0x3c, 0x7e, 0xff, 0x7e, 0x3c, 0x00,
};
static const uint8_t tile_ground[8] = {
    0xff, 0x81, 0xbd, 0xa5, 0xa5, 0xbd, 0x81, 0xff,
};
static const uint8_t tile_grass[8] = {
    0xff, 0xff, 0x81, 0xbd, 0xa5, 0xbd, 0x81, 0xff,
};
static const uint8_t tile_hazard[8] = {
    0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81,
};
static const uint8_t tile_coin[8] = {
    0x18, 0x3c, 0x66, 0x5a, 0x5a, 0x66, 0x3c, 0x18,
};
static const uint8_t tile_pipe[8] = {
    0xff, 0x99, 0xff, 0x81, 0xbd, 0xbd, 0xbd, 0xff,
};
static const uint8_t tile_flag[8] = {
    0x10, 0x1e, 0x1f, 0x1e, 0x10, 0x10, 0x10, 0x10,
};

static void put_run(uint8_t layer, uint8_t x0, uint8_t x1, uint8_t y, uint8_t tile) {
    for (uint8_t x = x0; x <= x1 && x < PRG32_PLAYFIELD_COLS; ++x) {
        prg32_playfield_put(layer, x, y, tile);
    }
}

static void put_column(uint8_t x, uint8_t y0, uint8_t y1, uint8_t tile) {
    for (uint8_t y = y0; y <= y1 && y < PRG32_PLAYFIELD_ROWS; ++y) {
        prg32_playfield_put(1, x, y, tile);
    }
}

static void fill_world(void) {
    prg32_playfield_clear(0, 0);
    prg32_playfield_clear(1, 0);

    for (uint8_t x = 3; x < PRG32_PLAYFIELD_COLS; x += 9) {
        prg32_playfield_put(0, x, 5 + (x & 3), 1);
    }

    for (uint8_t x = 0; x < 61; ++x) {
        if ((x >= 15 && x <= 17) ||
            (x >= 34 && x <= 36) ||
            (x >= 50 && x <= 51)) {
            continue;
        }
        prg32_playfield_put(1, x, 22, 3);
        prg32_playfield_put(1, x, 23, 2);
        prg32_playfield_put(1, x, 24, 2);
    }

    put_run(1, 7, 13, 17, 3);
    put_run(1, 21, 27, 15, 3);
    put_run(1, 39, 45, 18, 3);
    put_run(1, 55, 60, 14, 3);
    put_column(31, 19, 22, 6);
    put_column(32, 19, 22, 6);
    put_column(47, 17, 22, 6);
    put_column(48, 17, 22, 6);

    for (uint8_t x = 8; x <= 58; x += 5) {
        prg32_playfield_put(1, x, (x & 1) ? 12 : 14, 5);
    }

    prg32_playfield_put(1, 29, 21, 4);
    prg32_playfield_put(1, 52, 21, 4);
    prg32_playfield_put(1, 60, 13, 7);
}

static void reset_player(void) {
    prg32_platform_actor_init(&player, 1, 32, 120, 12, 16);
    prg32_playfield_camera(0, 0);
}

static void draw_uint2(int x, int y, uint16_t value, uint16_t color) {
    char text[3];
    value %= 100;
    text[0] = (char)('0' + value / 10);
    text[1] = (char)('0' + value % 10);
    text[2] = 0;
    prg32_gfx_text8(x, y, text, color, PRG32_COLOR_BLACK);
}

static void draw_player(int x, int y) {
    prg32_gfx_rect(x + 3, y, 6, 5, PRG32_COLOR_RED);
    prg32_gfx_rect(x + 2, y + 5, 8, 7, PRG32_COLOR_BLUE);
    prg32_gfx_rect(x, y + 7, 3, 4, 0xffde);
    prg32_gfx_rect(x + 9, y + 7, 3, 4, 0xffde);
    if (anim_frame) {
        prg32_gfx_rect(x + 2, y + 12, 3, 4, PRG32_COLOR_WHITE);
        prg32_gfx_rect(x + 8, y + 12, 3, 4, PRG32_COLOR_WHITE);
    } else {
        prg32_gfx_rect(x, y + 12, 4, 4, PRG32_COLOR_WHITE);
        prg32_gfx_rect(x + 8, y + 12, 4, 4, PRG32_COLOR_WHITE);
    }
}

static void collect_tiles(void) {
    int x0 = player.x / PRG32_TILE_W;
    int y0 = player.y / PRG32_TILE_H;
    int x1 = (player.x + (int)player.w - 1) / PRG32_TILE_W;
    int y1 = (player.y + (int)player.h - 1) / PRG32_TILE_H;

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (x < 0 || y < 0 ||
                x >= PRG32_PLAYFIELD_COLS ||
                y >= PRG32_PLAYFIELD_ROWS) {
                continue;
            }
            if (prg32_playfield_get(1, (uint8_t)x, (uint8_t)y) == 5) {
                prg32_playfield_put(1, (uint8_t)x, (uint8_t)y, 0);
                coins++;
                prg32_audio_beep(880, 35);
            }
        }
    }
}

void platformer_c_init(void) {
    prg32_tile_define(0, tile_empty, PRG32_COLOR_CYAN, PRG32_COLOR_CYAN);
    prg32_tile_define(1, tile_cloud, PRG32_COLOR_WHITE, PRG32_COLOR_BLUE);
    prg32_tile_define(2, tile_ground, PRG32_COLOR_GREEN, PRG32_COLOR_BLUE);
    prg32_tile_define(3, tile_grass, PRG32_COLOR_WHITE, PRG32_COLOR_GREEN);
    prg32_tile_define(4, tile_hazard, PRG32_COLOR_RED, PRG32_COLOR_BLACK);
    prg32_tile_define(5, tile_coin, PRG32_COLOR_YELLOW, PRG32_COLOR_BLACK);
    prg32_tile_define(6, tile_pipe, PRG32_COLOR_GREEN, 0x03c0);
    prg32_tile_define(7, tile_flag, PRG32_COLOR_WHITE, PRG32_COLOR_RED);

    prg32_platform_tile_flags(0, 0);
    prg32_platform_tile_flags(1, 0);
    prg32_platform_tile_flags(2, PRG32_TILE_FLAG_SOLID);
    prg32_platform_tile_flags(3, PRG32_TILE_FLAG_PLATFORM);
    prg32_platform_tile_flags(4, PRG32_TILE_FLAG_SOLID | PRG32_TILE_FLAG_HAZARD);
    prg32_platform_tile_flags(5, PRG32_TILE_FLAG_COLLECT);
    prg32_platform_tile_flags(6, PRG32_TILE_FLAG_SOLID);
    prg32_platform_tile_flags(7, PRG32_TILE_FLAG_COLLECT);
    prg32_playfield_parallax(0, 96, 128);
    fill_world();
    coins = 0;
    lives = 3;
    win_timer = 0;
    reset_player();
}

void platformer_c_update(void) {
    uint32_t input = prg32_input_read();
    if (input & PRG32_BTN_B) {
        fill_world();
        coins = 0;
        lives = 3;
        win_timer = 0;
        reset_player();
        return;
    }
    if (win_timer > 0) {
        win_timer--;
        if (win_timer == 0) {
            fill_world();
            reset_player();
        }
        return;
    }

    prg32_platform_actor_step(&player, input, 3, -10, 1, 7);
    collect_tiles();
    prg32_platform_camera_follow(&player, 56, 32);
    anim_frame = prg32_sprite_anim_frame(prg32_ticks_ms(), 2, 140);

    if ((player.state & PRG32_PLATFORM_HAZARD) || player.y > 210) {
        if (lives > 0) {
            lives--;
        }
        reset_player();
    }
    if (player.x > 472) {
        win_timer = 90;
        prg32_audio_beep(988, 80);
    }
}

void platformer_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_CYAN);
    prg32_playfield_draw_dual();
    draw_player(player.x - prg32_playfield_camera_x(),
                player.y - prg32_playfield_camera_y());
    prg32_gfx_text8(8, 8, "PLATFORMER C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 24, "COINS", PRG32_COLOR_YELLOW, PRG32_COLOR_BLACK);
    draw_uint2(56, 24, coins, PRG32_COLOR_YELLOW);
    prg32_gfx_text8(96, 24, "LIVES", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
    draw_uint2(144, 24, lives, PRG32_COLOR_WHITE);
    if (win_timer > 0) {
        prg32_gfx_text8(104, 88, "COURSE CLEAR", PRG32_COLOR_YELLOW, PRG32_COLOR_BLACK);
    }
}
