#include "prg32.h"

static int piece_x;
static int piece_y;
static int rotation;
static int fall_count;

void tetris_c_init(void) {
    piece_x = 4;
    piece_y = 0;
    rotation = 0;
    fall_count = 0;
}

void tetris_c_update(void) {
    uint32_t input = prg32_input_read();
    if (input & PRG32_BTN_LEFT) {
        piece_x--;
    }
    if (input & PRG32_BTN_RIGHT) {
        piece_x++;
    }
    if (input & PRG32_BTN_A) {
        rotation = (rotation + 1) & 3;
    }
    if (piece_x < 0) {
        piece_x = 0;
    }
    if (piece_x > 7) {
        piece_x = 7;
    }
    fall_count++;
    if ((input & PRG32_BTN_DOWN) || fall_count > 8) {
        fall_count = 0;
        piece_y++;
        if (piece_y > 17) {
            piece_y = 0;
        }
    }
}

static void cell(int tx, int ty) {
    prg32_gfx_rect(120 + tx * 8, 20 + ty * 8, 8, 8, PRG32_COLOR_YELLOW);
}

void tetris_c_draw(void) {
    static const int8_t shape[4][4][2] = {
        {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{1, 0}, {1, 1}, {2, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {2, 1}, {1, 2}},
        {{1, 0}, {0, 1}, {1, 1}, {1, 2}},
    };

    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_rect(118, 18, 84, 164, PRG32_COLOR_BLUE);
    prg32_gfx_rect(120, 20, 80, 160, PRG32_COLOR_BLACK);

    for (int i = 0; i < 4; ++i) {
        cell(piece_x + shape[rotation][i][0],
             piece_y + shape[rotation][i][1]);
    }
    prg32_gfx_text8(8, 8, "TETRIS C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
}
