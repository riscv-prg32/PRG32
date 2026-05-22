#include "prg32.h"

#define RAY_MAP_W 16
#define RAY_MAP_H 12
#define RAY_CELL 64
#define RAY_STRIPS 64

static const char map[RAY_MAP_H][RAY_MAP_W + 1] = {
    "1111111111111111",
    "1000000000000001",
    "1022200011110201",
    "1000200000010001",
    "1000202200010001",
    "1000000000010001",
    "1110111020010001",
    "1000100000010001",
    "1000102222010001",
    "1000000000000001",
    "1000001111000001",
    "1111111111111111",
};

static const int16_t dir_q8[32][2] = {
    {256,0},{251,50},{237,98},{213,142},{181,181},{142,213},{98,237},{50,251},
    {0,256},{-50,251},{-98,237},{-142,213},{-181,181},{-213,142},{-237,98},{-251,50},
    {-256,0},{-251,-50},{-237,-98},{-213,-142},{-181,-181},{-142,-213},{-98,-237},{-50,-251},
    {0,-256},{50,-251},{98,-237},{142,-213},{181,-181},{213,-142},{237,-98},{251,-50},
};

static int player_x;
static int player_y;
static int player_angle;
static uint32_t frame_count;

static int clamp_int(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int cell_at(int x, int y) {
    if (x < 0 || y < 0) {
        return '1';
    }

    int tx = x / RAY_CELL;
    int ty = y / RAY_CELL;
    if (tx < 0 || ty < 0 || tx >= RAY_MAP_W || ty >= RAY_MAP_H) {
        return '1';
    }
    return map[ty][tx];
}

static int open_at(int x, int y) {
    return cell_at(x, y) == '0';
}

static void try_move(int dx, int dy) {
    int nx = player_x + dx;
    int ny = player_y + dy;

    if (open_at(nx, player_y)) {
        player_x = nx;
    }
    if (open_at(player_x, ny)) {
        player_y = ny;
    }
}

static uint16_t wall_color(int cell, int dist) {
    if (dist > 420) {
        return cell == '2' ? 0x580b : 0x3186;
    }
    if (dist > 260) {
        return cell == '2' ? 0x9013 : 0x5aeb;
    }
    return cell == '2' ? PRG32_COLOR_MAGENTA : PRG32_COLOR_CYAN;
}

static void draw_minimap(void) {
    int ox = 8;
    int oy = 46;
    for (int y = 0; y < RAY_MAP_H; ++y) {
        for (int x = 0; x < RAY_MAP_W; ++x) {
            uint16_t color = map[y][x] == '0' ? 0x2104 : PRG32_COLOR_WHITE;
            if (map[y][x] == '2') {
                color = PRG32_COLOR_MAGENTA;
            }
            prg32_gfx_rect(ox + x * 4, oy + y * 4, 3, 3, color);
        }
    }
    prg32_gfx_rect(ox + (player_x / RAY_CELL) * 4,
                   oy + (player_y / RAY_CELL) * 4,
                   4,
                   4,
                   PRG32_COLOR_YELLOW);
}

static void draw_weapon(void) {
    prg32_gfx_rect(150, 166, 20, 20, 0x7bef);
    prg32_gfx_rect(144, 184, 32, 10, 0x4208);
    prg32_gfx_rect(157, 154, 6, 18, PRG32_COLOR_BLACK);
    if ((frame_count & 15u) < 2u) {
        prg32_gfx_rect(154, 146, 12, 8, PRG32_COLOR_YELLOW);
    }
}

void raycaster_c_init(void) {
    player_x = RAY_CELL * 2 + RAY_CELL / 2;
    player_y = RAY_CELL * 8 + RAY_CELL / 2;
    player_angle = 30;
    frame_count = 0;
}

void raycaster_c_update(void) {
    uint32_t input = prg32_input_read();

    if (input & PRG32_BTN_LEFT) {
        player_angle = (player_angle + 31) & 31;
    }
    if (input & PRG32_BTN_RIGHT) {
        player_angle = (player_angle + 1) & 31;
    }

    int forward_x = (dir_q8[player_angle][0] * 8) / 256;
    int forward_y = (dir_q8[player_angle][1] * 8) / 256;
    int strafe_angle = (player_angle + 8) & 31;
    int strafe_x = (dir_q8[strafe_angle][0] * 7) / 256;
    int strafe_y = (dir_q8[strafe_angle][1] * 7) / 256;

    if (input & PRG32_BTN_UP) {
        try_move(forward_x, forward_y);
    }
    if (input & PRG32_BTN_DOWN) {
        try_move(-forward_x, -forward_y);
    }
    if (input & PRG32_BTN_A) {
        try_move(-strafe_x, -strafe_y);
    }
    if (input & PRG32_BTN_B) {
        try_move(strafe_x, strafe_y);
    }

    frame_count++;
}

void raycaster_c_draw(void) {
    prg32_gfx_rect(0, 0, PRG32_GAME_W, PRG32_GAME_H / 2, 0x18e3);
    prg32_gfx_rect(0, PRG32_GAME_H / 2, PRG32_GAME_W, PRG32_GAME_H / 2, 0x4208);

    for (int strip = 0; strip < RAY_STRIPS; ++strip) {
        int angle = (player_angle + 32 + (strip - RAY_STRIPS / 2) / 5) & 31;
        int dist = 8;
        int hit = '1';

        for (; dist < 760; dist += 8) {
            int hx = player_x + (dir_q8[angle][0] * dist) / 256;
            int hy = player_y + (dir_q8[angle][1] * dist) / 256;
            hit = cell_at(hx, hy);
            if (hit != '0') {
                break;
            }
        }

        int height = 11200 / clamp_int(dist, 24, 760);
        height = clamp_int(height, 8, 160);
        int y = clamp_int(100 - height / 2, 20, 190 - height);
        uint16_t color = wall_color(hit, dist);
        prg32_gfx_rect(strip * 5, y, 5, height, color);
        if ((strip & 1) == 0) {
            prg32_gfx_rect(strip * 5, y, 1, height, 0x2104);
        }
    }

    draw_minimap();
    draw_weapon();
    prg32_gfx_text8(8, 8, "RAYCASTER C", PRG32_COLOR_WHITE, PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 184, "MOVE UP/DOWN  TURN L/R  STRAFE A/B", PRG32_COLOR_YELLOW, PRG32_COLOR_BLACK);
}
