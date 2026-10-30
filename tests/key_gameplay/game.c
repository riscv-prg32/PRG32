/*
 * Key gameplay check: a small deterministic game for testing key commands.
 *
 * Move on the grid with the D-pad, collect the three coins, avoid the hazards.
 * START begins the game and pauses it, B pauses it, holding A charges a shot
 * fired on release, and A+B together is a special move. There is no randomness,
 * so a sequence of keys always gives the same result.
 *
 * Every frame the whole game state is drawn as a strip of white and black cells
 * (the top right of the screen), so play.py can read exact values from a
 * screenshot instead of guessing from pictures.
 */
#include "prg32.h"

#define GRID_SIZE 11
#define CELL 16
#define GRID_X 4
#define GRID_Y 12
#define COIN_COUNT 3
#define START_LIVES 3
#define CHARGE_FRAMES 3
#define COIN_SCORE 10
#define SHOT_SCORE 5

#define STRIP_X 184
#define STRIP_Y 0
#define STRIP_BITS 16
#define STRIP_BIT_W 8
#define STRIP_ROW_H 4
#define STRIP_MAGIC 0xC5A3u

enum { STATE_TITLE, STATE_PLAY, STATE_PAUSE, STATE_WIN, STATE_OVER };

typedef struct {
    int x;
    int y;
} cell_t;

static const cell_t WALLS[] = {{2, 0}, {2, 1}, {2, 2}};
static const cell_t HAZARDS[] = {{4, 0}, {0, 1}};
static const cell_t COINS[COIN_COUNT] = {{1, 0}, {3, 3}, {5, 1}};

/* Order matters: play.py reads the strip rows in this order. */
static struct {
    uint32_t state;
    uint32_t x;
    uint32_t y;
    uint32_t score;
    uint32_t lives;
    uint32_t moves;
    uint32_t blocked;
    uint32_t a_count;
    uint32_t b_count;
    uint32_t start_count;
    uint32_t chord_count;
    uint32_t shots;
    uint32_t held_frames;
    uint32_t ignored;
    uint32_t frame;
} game;

static uint8_t coin_taken[COIN_COUNT];
static uint32_t last_input;
static uint32_t charge;
static uint8_t chord_active;

static int cell_in_list(const cell_t *list, int count, int x, int y) {
    for (int i = 0; i < count; ++i) {
        if (list[i].x == x && list[i].y == y) {
            return i;
        }
    }
    return -1;
}

/* Cartridges link without the C library, so there is no memset here. */
static void reset_round(void) {
    game.x = 0;
    game.y = 0;
    game.score = 0;
    game.lives = START_LIVES;
    game.moves = 0;
    game.blocked = 0;
    game.a_count = 0;
    game.b_count = 0;
    game.chord_count = 0;
    game.shots = 0;
    game.held_frames = 0;
    game.ignored = 0;
    for (int i = 0; i < COIN_COUNT; ++i) {
        coin_taken[i] = 0;
    }
    charge = 0;
    chord_active = 0;
}

static void start_game(void) {
    reset_round();
    game.state = STATE_PLAY;
}

static void try_move(int dx, int dy) {
    int x = (int)game.x + dx;
    int y = (int)game.y + dy;
    game.moves++;
    if (x < 0 || y < 0 || x >= GRID_SIZE || y >= GRID_SIZE ||
        cell_in_list(WALLS, 3, x, y) >= 0) {
        game.blocked++;
        return;
    }
    game.x = (uint32_t)x;
    game.y = (uint32_t)y;

    if (cell_in_list(HAZARDS, 2, x, y) >= 0) {
        game.x = 0;
        game.y = 0;
        game.lives--;
        if (game.lives == 0) {
            game.state = STATE_OVER;
        }
        return;
    }
    int coin = cell_in_list(COINS, COIN_COUNT, x, y);
    if (coin >= 0 && !coin_taken[coin]) {
        coin_taken[coin] = 1;
        game.score += COIN_SCORE;
        int taken = 0;
        for (int i = 0; i < COIN_COUNT; ++i) {
            taken += coin_taken[i];
        }
        if (taken == COIN_COUNT) {
            game.state = STATE_WIN;
        }
    }
}

static void update_play(uint32_t input, uint32_t pressed, uint32_t released) {
    if (pressed & PRG32_BTN_LEFT) try_move(-1, 0);
    if (pressed & PRG32_BTN_RIGHT) try_move(1, 0);
    if (pressed & PRG32_BTN_UP) try_move(0, -1);
    if (pressed & PRG32_BTN_DOWN) try_move(0, 1);
    if (input & PRG32_BTN_RIGHT) {
        game.held_frames++;
    }

    /* Both buttons going down in one frame is the special move. */
    if ((pressed & PRG32_BTN_A) && (pressed & PRG32_BTN_B)) {
        game.chord_count++;
        chord_active = 1;
        return;
    }
    if (chord_active) {
        if ((input & (PRG32_BTN_A | PRG32_BTN_B)) == 0) {
            chord_active = 0;
        }
        return;
    }
    if (pressed & PRG32_BTN_A) {
        game.a_count++;
        charge = 0;
    }
    if (input & PRG32_BTN_A) {
        charge++;
    }
    if (released & PRG32_BTN_A) {
        if (charge >= CHARGE_FRAMES) {
            game.shots++;
            game.score += SHOT_SCORE;
        }
        charge = 0;
    }
    if (pressed & PRG32_BTN_B) {
        game.b_count++;
        game.state = STATE_PAUSE;
    }
    if (pressed & PRG32_BTN_START) {
        game.state = STATE_PAUSE;
    }
}

void keyplay_init(void) {
    reset_round();
    game.start_count = 0;
    game.frame = 0;
    last_input = 0;
    game.state = STATE_TITLE;
}

void keyplay_update(void) {
    uint32_t input = prg32_input_read();
    uint32_t pressed = input & ~last_input;
    uint32_t released = last_input & ~input;
    last_input = input;
    game.frame++;

    if (pressed & PRG32_BTN_START) {
        game.start_count++;
    }
    switch (game.state) {
    case STATE_TITLE:
        if (pressed & PRG32_BTN_START) {
            start_game();
        } else if (pressed) {
            game.ignored++;
        }
        break;
    case STATE_PLAY:
        update_play(input, pressed, released);
        break;
    case STATE_PAUSE:
        if (pressed & PRG32_BTN_B) {
            game.b_count++;
            game.state = STATE_PLAY;
        } else if (pressed & PRG32_BTN_START) {
            game.state = STATE_PLAY;
        } else if (pressed) {
            game.ignored++;
        }
        break;
    default: /* STATE_WIN and STATE_OVER */
        if (pressed & PRG32_BTN_START) {
            game.state = STATE_TITLE;
        } else if (pressed) {
            game.ignored++;
        }
        break;
    }
}

static void draw_cell(int x, int y, uint16_t color, int inset) {
    prg32_gfx_rect(GRID_X + x * CELL + inset, GRID_Y + y * CELL + inset,
                   CELL - 2 * inset, CELL - 2 * inset, color);
}

static void draw_strip(void) {
    const uint32_t *fields = (const uint32_t *)&game;
    int count = (int)(sizeof(game) / sizeof(uint32_t));
    uint32_t check = STRIP_MAGIC;
    for (int row = 0; row < count + 2; ++row) {
        uint32_t value = row == 0 ? STRIP_MAGIC
                       : row <= count ? fields[row - 1] & 0xffffu
                                      : check;
        if (row > 0 && row <= count) {
            check ^= value;
        }
        for (int bit = 0; bit < STRIP_BITS; ++bit) {
            prg32_gfx_rect(STRIP_X + bit * STRIP_BIT_W, STRIP_Y + row * STRIP_ROW_H,
                           STRIP_BIT_W, STRIP_ROW_H,
                           (value >> bit) & 1u ? PRG32_COLOR_WHITE : PRG32_COLOR_BLACK);
        }
    }
}

/* A switch, not a table of strings: pointers stored in data are linked for the
 * cartridge's link address, not the address it runs from. */
static const char *state_name(void) {
    switch (game.state) {
    case STATE_TITLE: return "PRESS START";
    case STATE_PLAY: return "PLAY";
    case STATE_PAUSE: return "PAUSED";
    case STATE_WIN: return "YOU WIN";
    default: return "GAME OVER";
    }
}

void keyplay_draw(void) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);

    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            draw_cell(x, y, 0x2104u, 1);
        }
    }
    for (int i = 0; i < 3; ++i) draw_cell(WALLS[i].x, WALLS[i].y, PRG32_COLOR_BLUE, 0);
    for (int i = 0; i < 2; ++i) draw_cell(HAZARDS[i].x, HAZARDS[i].y, PRG32_COLOR_RED, 1);
    for (int i = 0; i < COIN_COUNT; ++i) {
        if (!coin_taken[i]) draw_cell(COINS[i].x, COINS[i].y, PRG32_COLOR_YELLOW, 4);
    }
    draw_cell((int)game.x, (int)game.y, PRG32_COLOR_GREEN, 2);

    prg32_gfx_text8(4, 0, state_name(), PRG32_COLOR_WHITE, 0);
    draw_strip();
}
