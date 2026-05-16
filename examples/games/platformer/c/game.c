#include "prg32.h"

static prg32_platform_actor_t player;
static uint32_t anim_frame;

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

static const uint16_t player_frames[2][64] = {
    {
        0xffff,0xffff,0x07e0,0x07e0,0x07e0,0x07e0,0xffff,0xffff,
        0xffff,0x07e0,0xffff,0xffff,0xffff,0xffff,0x07e0,0xffff,
        0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,
        0x07e0,0xffff,0xf800,0xffff,0xffff,0xf800,0xffff,0x07e0,
        0x07e0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x07e0,
        0xffff,0x07e0,0xffff,0xffff,0xffff,0xffff,0x07e0,0xffff,
        0xffff,0xffff,0x001f,0xffff,0xffff,0x001f,0xffff,0xffff,
        0xffff,0x001f,0xffff,0xffff,0xffff,0xffff,0x001f,0xffff,
    },
    {
        0xffff,0xffff,0xffe0,0xffe0,0xffe0,0xffe0,0xffff,0xffff,
        0xffff,0xffe0,0xffff,0xffff,0xffff,0xffff,0xffe0,0xffff,
        0xffe0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffe0,
        0xffe0,0xffff,0xf800,0xffff,0xffff,0xf800,0xffff,0xffe0,
        0xffe0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffe0,
        0xffff,0xffe0,0xffff,0xffff,0xffff,0xffff,0xffe0,0xffff,
        0x001f,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0x001f,
        0xffff,0x001f,0xffff,0xffff,0xffff,0xffff,0x001f,0xffff,
    },
};

static void fill_world(void) {
    prg32_playfield_clear(0, 0);
    prg32_playfield_clear(1, 0);

    for (uint8_t x = 0; x < PRG32_PLAYFIELD_COLS; x += 8) {
        prg32_playfield_put(0, x, 5, 1);
    }
    for (uint8_t x = 0; x < PRG32_PLAYFIELD_COLS; ++x) {
        prg32_playfield_put(1, x, 22, 3);
        prg32_playfield_put(1, x, 23, 2);
    }
    for (uint8_t x = 10; x < 20; ++x) {
        prg32_playfield_put(1, x, 16, 3);
    }
    prg32_playfield_put(1, 28, 21, 4);
}

void platformer_c_init(void) {
    prg32_tile_define(0, tile_empty, PRG32_COLOR_BLACK, PRG32_COLOR_BLACK);
    prg32_tile_define(1, tile_cloud, PRG32_COLOR_WHITE, PRG32_COLOR_BLUE);
    prg32_tile_define(2, tile_ground, PRG32_COLOR_GREEN, PRG32_COLOR_BLUE);
    prg32_tile_define(3, tile_grass, PRG32_COLOR_WHITE, PRG32_COLOR_GREEN);
    prg32_tile_define(4, tile_hazard, PRG32_COLOR_RED, PRG32_COLOR_BLACK);

    prg32_platform_tile_flags(2, PRG32_TILE_FLAG_SOLID);
    prg32_platform_tile_flags(3, PRG32_TILE_FLAG_PLATFORM);
    prg32_platform_tile_flags(4, PRG32_TILE_FLAG_SOLID | PRG32_TILE_FLAG_HAZARD);
    prg32_playfield_parallax(0, 96, 128);
    fill_world();
    prg32_platform_actor_init(&player, 1, 32, 120, 8, 8);
}

void platformer_c_update(void) {
    uint32_t input = prg32_input_read();
    prg32_platform_actor_step(&player, input, 2, -7, 1, 5);
    prg32_platform_camera_follow(&player, 56, 32);
    anim_frame = prg32_sprite_anim_frame(prg32_ticks_ms(), 2, 140);
}

void platformer_c_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_playfield_draw_dual();
    prg32_sprite_draw_frame(player.x - prg32_playfield_camera_x(),
                            player.y - prg32_playfield_camera_y(),
                            8,
                            8,
                            &player_frames[0][0],
                            anim_frame,
                            PRG32_COLOR_WHITE);
    prg32_gfx_text8(8, 8, "PLATFORMER C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
}
