#include "prg32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define DEMO_PAGE_COUNT 9

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
    prg32_gfx_clear(PRG32_COLOR_BLACK);
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

static void draw_pong(uint32_t frame) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("PONG-INSPIRED DEMO", "TWO PADDLES AND A BALL");
    for (int y = 44; y < 174; y += 16) {
        prg32_gfx_rect(158, y, 4, 8, PRG32_COLOR_BLUE);
    }
    int ball_x = 40 + (int)((frame * 3u) % 236u);
    int ball_y = 56 + (int)((frame * 2u) % 92u);
    prg32_gfx_rect(20, 72 + (int)(frame % 48u), 8, 42, PRG32_COLOR_WHITE);
    prg32_gfx_rect(292, 56 + (int)((frame * 2u) % 60u), 8, 42, PRG32_COLOR_WHITE);
    prg32_gfx_rect(ball_x, ball_y, 8, 8, PRG32_COLOR_YELLOW);
    prg32_gfx_text8(128, 36, "01  02", PRG32_COLOR_CYAN, 0);
    draw_footer();
}

static void draw_breakout(uint32_t frame) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("BREAKOUT-INSPIRED DEMO", "BRICKS, PADDLE, BOUNCE");
    for (int row = 0; row < 4; ++row) {
        uint16_t color = row & 1 ? PRG32_COLOR_MAGENTA : PRG32_COLOR_CYAN;
        for (int col = 0; col < 10; ++col) {
            if (((col + row + (int)(frame / 20u)) % 9) == 0) {
                continue;
            }
            prg32_gfx_rect(14 + col * 30, 48 + row * 12, 26, 8, color);
        }
    }
    prg32_gfx_rect(124 + (int)(frame % 56u), 170, 72, 8, PRG32_COLOR_WHITE);
    prg32_gfx_rect(72 + (int)((frame * 2u) % 160u),
                   110 + (int)(frame % 36u),
                   8,
                   8,
                   PRG32_COLOR_YELLOW);
    draw_footer();
}

static void draw_space_invaders(uint32_t frame) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("SPACE INVADERS DEMO", "FORMATION + PLAYER SHIP");
    int wave = (int)((frame / 8u) % 16u);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 9; ++col) {
            int x = 32 + col * 28 + wave;
            int y = 48 + row * 22;
            prg32_gfx_rect(x, y, 16, 8, PRG32_COLOR_GREEN);
            prg32_gfx_rect(x + 4, y + 8, 8, 4, PRG32_COLOR_GREEN);
        }
    }
    int ship_x = 132 + (int)((frame * 2u) % 64u);
    prg32_gfx_rect(ship_x, 168, 28, 8, PRG32_COLOR_CYAN);
    prg32_gfx_rect(ship_x + 10, 160, 8, 8, PRG32_COLOR_CYAN);
    prg32_gfx_rect(ship_x + 14, 120 - (int)((frame * 4u) % 70u), 2, 12, PRG32_COLOR_WHITE);
    draw_footer();
}

static void draw_pacman(uint32_t frame) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("PACMAN-INSPIRED DEMO", "MAZE, DOTS, CHARACTER");
    for (int y = 48; y <= 160; y += 32) {
        prg32_gfx_rect(24, y, 272, 4, PRG32_COLOR_BLUE);
    }
    for (int x = 24; x <= 296; x += 48) {
        prg32_gfx_rect(x, 48, 4, 116, PRG32_COLOR_BLUE);
    }
    for (int x = 42; x < 280; x += 24) {
        prg32_gfx_rect(x, 72, 4, 4, PRG32_COLOR_WHITE);
        prg32_gfx_rect(x, 136, 4, 4, PRG32_COLOR_WHITE);
    }
    int px = 40 + (int)((frame * 2u) % 220u);
    int mouth = (frame / 5u) & 1u;
    prg32_gfx_rect(px, 96, 16, 16, PRG32_COLOR_YELLOW);
    if (mouth) {
        prg32_gfx_rect(px + 10, 100, 6, 8, PRG32_COLOR_BLACK);
    }
    prg32_gfx_rect(240, 96, 14, 14, PRG32_COLOR_MAGENTA);
    draw_footer();
}

static void draw_tetris(uint32_t frame) {
    static const uint16_t colors[] = {
        PRG32_COLOR_CYAN,
        PRG32_COLOR_YELLOW,
        PRG32_COLOR_MAGENTA,
        PRG32_COLOR_GREEN,
    };
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("TETRIS-INSPIRED DEMO", "FALLING BLOCKS");
    prg32_gfx_rect(110, 42, 100, 132, PRG32_COLOR_BLUE);
    prg32_gfx_rect(114, 46, 92, 124, PRG32_COLOR_BLACK);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (((x + y) & 3) == 0) {
                prg32_gfx_rect(118 + x * 10, 150 - y * 10, 8, 8,
                               colors[(x + y) & 3]);
            }
        }
    }
    int falling_y = 46 + (int)((frame * 2u) % 80u);
    prg32_gfx_rect(150, falling_y, 18, 8, PRG32_COLOR_CYAN);
    prg32_gfx_rect(160, falling_y + 10, 8, 8, PRG32_COLOR_CYAN);
    draw_footer();
}

static void draw_pole_position(uint32_t frame) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_title("POLE POSITION DEMO", "PERSPECTIVE ROAD");
    prg32_gfx_rect(0, 40, PRG32_GAME_W, 54, PRG32_COLOR_CYAN);
    prg32_gfx_rect(0, 94, PRG32_GAME_W, 106, PRG32_COLOR_GREEN);
    draw_line(72, 199, 142, 94, PRG32_COLOR_WHITE);
    draw_line(248, 199, 178, 94, PRG32_COLOR_WHITE);
    draw_line(160, 94, 160, 199, PRG32_COLOR_YELLOW);
    for (int i = 0; i < 6; ++i) {
        int y = 108 + i * 18 + (int)((frame * 2u) % 18u);
        int w = 8 + i * 6;
        prg32_gfx_rect(160 - w / 2, y, w, 4, PRG32_COLOR_YELLOW);
    }
    int car_x = 144 + (int)((frame % 40u) - 20);
    prg32_gfx_rect(car_x, 164, 32, 14, PRG32_COLOR_RED);
    prg32_gfx_rect(car_x + 8, 154, 16, 10, PRG32_COLOR_RED);
    draw_footer();
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
            prg32_audio_beep(880, 40);
            prg32_input_wait_released(PRG32_BTN_B | PRG32_BTN_SELECT);
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
            default: draw_pole_position(frame); break;
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
        vTaskDelay(pdMS_TO_TICKS(33));
    }

    prg32_band_set_game_info("");
    prg32_band_set_mode(PRG32_BAND_TOP, top_mode);
    prg32_band_set_mode(PRG32_BAND_BOTTOM, bottom_mode);
    prg32_tile_clear(PRG32_COLOR_BLACK);
    prg32_playfield_clear(0, 0);
    prg32_playfield_clear(1, 0);
    prg32_gfx_set_fullscreen(was_fullscreen);
}
