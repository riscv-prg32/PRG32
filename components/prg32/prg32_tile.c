#include "prg32.h"
#include <string.h>

typedef struct {
    uint8_t bits[8];
    uint16_t fg;
    uint16_t bg;
} tile_t;

static tile_t g_tiles[256];
static uint8_t g_map[PRG32_TILE_ROWS][PRG32_TILE_COLS];
static uint8_t g_dirty[PRG32_TILE_ROWS][PRG32_TILE_COLS];
static uint8_t g_playfield[PRG32_PLAYFIELD_LAYERS]
                           [PRG32_PLAYFIELD_ROWS]
                           [PRG32_PLAYFIELD_COLS];
static int g_scroll_x[PRG32_PLAYFIELD_LAYERS];
static int g_scroll_y[PRG32_PLAYFIELD_LAYERS];
static int g_parallax_x_q8[PRG32_PLAYFIELD_LAYERS] = {
    PRG32_PARALLAX_1X,
    PRG32_PARALLAX_1X,
};
static int g_parallax_y_q8[PRG32_PLAYFIELD_LAYERS] = {
    PRG32_PARALLAX_1X,
    PRG32_PARALLAX_1X,
};
static int g_camera_x;
static int g_camera_y;

static int valid_layer(uint8_t layer) {
    return layer < PRG32_PLAYFIELD_LAYERS;
}

static int floor_div_int(int value, int divisor) {
    if (divisor <= 0) {
        return 0;
    }
    if (value >= 0) {
        return value / divisor;
    }
    return -((-value + divisor - 1) / divisor);
}

static int wrap_index(int value, int limit) {
    int wrapped = value % limit;
    if (wrapped < 0) {
        wrapped += limit;
    }
    return wrapped;
}

static int camera_scaled(int camera, int q8) {
    return (int)(((int64_t)camera * (int64_t)q8) / PRG32_PARALLAX_1X);
}

static void draw_tile_pixels(uint8_t id,
                             int x,
                             int y,
                             int transparent_zero) {
    if (transparent_zero && id == 0) {
        return;
    }

    tile_t *tile = &g_tiles[id];
    for (int row = 0; row < PRG32_TILE_H; ++row) {
        int py = y + row;
        if ((unsigned)py >= PRG32_GAME_H) {
            continue;
        }
        for (int col = 0; col < PRG32_TILE_W; ++col) {
            int px = x + col;
            if ((unsigned)px >= PRG32_GAME_W) {
                continue;
            }
            uint16_t color = (tile->bits[row] & (1u << (7 - col)))
                ? tile->fg
                : tile->bg;
            prg32_gfx_pixel(px, py, color);
        }
    }
}

void prg32_tile_clear(uint16_t color) {
    memset(g_map, 0, sizeof(g_map));
    memset(g_dirty, 1, sizeof(g_dirty));
    memset(g_tiles[0].bits, 0, sizeof(g_tiles[0].bits));
    g_tiles[0].fg = color;
    g_tiles[0].bg = color;
}

void prg32_tile_define(uint8_t id,
                       const uint8_t *bitmap8x8,
                       uint16_t fg,
                       uint16_t bg) {
    if (bitmap8x8) {
        memcpy(g_tiles[id].bits, bitmap8x8, 8);
    } else {
        memset(g_tiles[id].bits, 0, sizeof(g_tiles[id].bits));
    }
    g_tiles[id].fg = fg;
    g_tiles[id].bg = bg;
    for (int ty = 0; ty < PRG32_TILE_ROWS; ++ty) {
        for (int tx = 0; tx < PRG32_TILE_COLS; ++tx) {
            if (g_map[ty][tx] == id) {
                g_dirty[ty][tx] = 1;
            }
        }
    }
}

void prg32_tile_put(uint8_t tx, uint8_t ty, uint8_t id) {
    if (tx >= PRG32_TILE_COLS || ty >= PRG32_TILE_ROWS) {
        return;
    }
    if (g_map[ty][tx] == id) {
        return;
    }
    g_map[ty][tx] = id;
    g_dirty[ty][tx] = 1;
}

void prg32_tile_present(void) {
    for (int ty = 0; ty < PRG32_TILE_ROWS; ++ty) {
        for (int tx = 0; tx < PRG32_TILE_COLS; ++tx) {
            if (!g_dirty[ty][tx]) {
                continue;
            }
            g_dirty[ty][tx] = 0;
            draw_tile_pixels(g_map[ty][tx], tx * 8, ty * 8, 0);
        }
    }
    prg32_gfx_present();
}

void prg32_playfield_clear(uint8_t layer, uint8_t tile_id) {
    if (!valid_layer(layer)) {
        return;
    }
    memset(g_playfield[layer], tile_id, sizeof(g_playfield[layer]));
    g_scroll_x[layer] = 0;
    g_scroll_y[layer] = 0;
    g_parallax_x_q8[layer] = PRG32_PARALLAX_1X;
    g_parallax_y_q8[layer] = PRG32_PARALLAX_1X;
}

void prg32_playfield_put(uint8_t layer, uint8_t tx, uint8_t ty, uint8_t id) {
    if (!valid_layer(layer) ||
        tx >= PRG32_PLAYFIELD_COLS ||
        ty >= PRG32_PLAYFIELD_ROWS) {
        return;
    }
    g_playfield[layer][ty][tx] = id;
}

uint8_t prg32_playfield_get(uint8_t layer, uint8_t tx, uint8_t ty) {
    if (!valid_layer(layer) ||
        tx >= PRG32_PLAYFIELD_COLS ||
        ty >= PRG32_PLAYFIELD_ROWS) {
        return 0;
    }
    return g_playfield[layer][ty][tx];
}

void prg32_playfield_scroll(uint8_t layer, int x, int y) {
    if (!valid_layer(layer)) {
        return;
    }
    g_scroll_x[layer] = x;
    g_scroll_y[layer] = y;
}

void prg32_playfield_scroll_by(uint8_t layer, int dx, int dy) {
    if (!valid_layer(layer)) {
        return;
    }
    g_scroll_x[layer] += dx;
    g_scroll_y[layer] += dy;
}

void prg32_playfield_parallax(uint8_t layer, int x_q8, int y_q8) {
    if (!valid_layer(layer)) {
        return;
    }
    g_parallax_x_q8[layer] = x_q8;
    g_parallax_y_q8[layer] = y_q8;
}

void prg32_playfield_camera(int x, int y) {
    g_camera_x = x;
    g_camera_y = y;
}

int prg32_playfield_camera_x(void) {
    return g_camera_x;
}

int prg32_playfield_camera_y(void) {
    return g_camera_y;
}

void prg32_playfield_draw(uint8_t layer, int transparent_zero) {
    if (!valid_layer(layer)) {
        return;
    }

    int origin_x = g_scroll_x[layer] +
        camera_scaled(g_camera_x, g_parallax_x_q8[layer]);
    int origin_y = g_scroll_y[layer] +
        camera_scaled(g_camera_y, g_parallax_y_q8[layer]);
    int start_tx = floor_div_int(origin_x, PRG32_TILE_W);
    int start_ty = floor_div_int(origin_y, PRG32_TILE_H);
    int offset_x = origin_x - start_tx * PRG32_TILE_W;
    int offset_y = origin_y - start_ty * PRG32_TILE_H;

    for (int sy = -offset_y, row = 0;
         sy < PRG32_GAME_H;
         sy += PRG32_TILE_H, ++row) {
        int map_y = wrap_index(start_ty + row, PRG32_PLAYFIELD_ROWS);
        for (int sx = -offset_x, col = 0;
             sx < PRG32_GAME_W;
             sx += PRG32_TILE_W, ++col) {
            int map_x = wrap_index(start_tx + col, PRG32_PLAYFIELD_COLS);
            uint8_t id = g_playfield[layer][map_y][map_x];
            draw_tile_pixels(id, sx, sy, transparent_zero);
        }
    }
}

void prg32_playfield_draw_dual(void) {
    prg32_playfield_draw(0, 0);
    prg32_playfield_draw(1, 1);
}

void prg32_playfield_present(void) {
    prg32_playfield_draw_dual();
    prg32_gfx_present();
}
