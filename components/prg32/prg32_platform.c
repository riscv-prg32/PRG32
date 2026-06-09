#include "prg32.h"
#include <stddef.h>
#include <string.h>
#include "esp_heap_caps.h"

static uint8_t *g_tile_flags;

typedef char actor_size_must_match_abi[
    sizeof(prg32_platform_actor_t) == PRG32_PLATFORM_ACTOR_SIZE ? 1 : -1
];
typedef char actor_state_offset_must_match_abi[
    offsetof(prg32_platform_actor_t, state) ==
    PRG32_PLATFORM_ACTOR_STATE_OFFSET ? 1 : -1
];

static int clamp_int(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int tile_flags_ready(void) {
    if (g_tile_flags) {
        return 1;
    }
    g_tile_flags = heap_caps_calloc(256,
                                    sizeof(g_tile_flags[0]),
                                    MALLOC_CAP_8BIT);
    return g_tile_flags != NULL;
}

void prg32_platform_tile_flags(uint8_t tile_id, uint8_t flags) {
    if (!tile_flags_ready()) {
        return;
    }
    g_tile_flags[tile_id] = flags;
}

uint8_t prg32_platform_tile_flags_get(uint8_t tile_id) {
    if (!g_tile_flags) {
        return 0;
    }
    return g_tile_flags[tile_id];
}

uint8_t prg32_platform_tile_at(uint8_t layer, int pixel_x, int pixel_y) {
    if (pixel_x < 0 || pixel_y < 0) {
        return 0;
    }

    int tx = pixel_x / PRG32_TILE_W;
    int ty = pixel_y / PRG32_TILE_H;
    if (tx >= PRG32_PLAYFIELD_COLS || ty >= PRG32_PLAYFIELD_ROWS) {
        return 0;
    }

    return prg32_playfield_get(layer, (uint8_t)tx, (uint8_t)ty);
}

int prg32_platform_solid_at(uint8_t layer, int pixel_x, int pixel_y) {
    uint8_t tile = prg32_platform_tile_at(layer, pixel_x, pixel_y);
    if (!g_tile_flags) {
        return 0;
    }
    return (g_tile_flags[tile] & PRG32_TILE_FLAG_SOLID) != 0;
}

void prg32_platform_actor_init(prg32_platform_actor_t *actor,
                               uint8_t layer,
                               int x,
                               int y,
                               int w,
                               int h) {
    if (!actor) {
        return;
    }
    memset(actor, 0, sizeof(*actor));
    actor->x = x;
    actor->y = y;
    actor->w = (uint16_t)(w > 0 ? w : 1);
    actor->h = (uint16_t)(h > 0 ? h : 1);
    actor->layer = layer;
}

static uint8_t tile_flags_at(uint8_t layer, int pixel_x, int pixel_y) {
    uint8_t tile = prg32_platform_tile_at(layer, pixel_x, pixel_y);
    if (!g_tile_flags) {
        return 0;
    }
    return g_tile_flags[tile];
}

static uint8_t actor_flags_overlapping(const prg32_platform_actor_t *actor) {
    if (!actor) {
        return 0;
    }

    uint8_t flags = 0;
    int x0 = actor->x;
    int y0 = actor->y;
    int x1 = actor->x + (int)actor->w - 1;
    int y1 = actor->y + (int)actor->h - 1;

    for (int y = y0; y <= y1; y += PRG32_TILE_H) {
        for (int x = x0; x <= x1; x += PRG32_TILE_W) {
            flags |= tile_flags_at(actor->layer, x, y);
        }
        flags |= tile_flags_at(actor->layer, x1, y);
    }
    for (int x = x0; x <= x1; x += PRG32_TILE_W) {
        flags |= tile_flags_at(actor->layer, x, y1);
    }
    flags |= tile_flags_at(actor->layer, x1, y1);
    return flags;
}

static uint16_t actor_state_from_flags(uint8_t flags) {
    uint16_t state = 0;
    if (flags & PRG32_TILE_FLAG_HAZARD) {
        state |= PRG32_PLATFORM_HAZARD;
    }
    if (flags & PRG32_TILE_FLAG_COLLECT) {
        state |= PRG32_PLATFORM_COLLECT;
    }
    return state;
}

static int actor_hits_platform_floor(const prg32_platform_actor_t *actor,
                                     int previous_y) {
    if (!actor) {
        return 0;
    }
    int old_bottom = previous_y + (int)actor->h - 1;
    int new_bottom = actor->y + (int)actor->h - 1;
    if (new_bottom < old_bottom) {
        return 0;
    }

    int x0 = actor->x;
    int x1 = actor->x + (int)actor->w - 1;
    int tile_y = (new_bottom / PRG32_TILE_H) * PRG32_TILE_H;
    if (old_bottom >= tile_y) {
        return 0;
    }

    for (int x = x0; x <= x1; x += PRG32_TILE_W) {
        if (tile_flags_at(actor->layer, x, new_bottom) &
            PRG32_TILE_FLAG_PLATFORM) {
            return 1;
        }
    }
    return (tile_flags_at(actor->layer, x1, new_bottom) &
            PRG32_TILE_FLAG_PLATFORM) != 0;
}

uint16_t prg32_platform_actor_move(prg32_platform_actor_t *actor,
                                   int dx,
                                   int dy) {
    if (!actor) {
        return 0;
    }

    uint16_t state = actor->state &
        (PRG32_PLATFORM_HAZARD | PRG32_PLATFORM_COLLECT);
    int step_x = dx < 0 ? -1 : 1;
    int step_y = dy < 0 ? -1 : 1;

    for (int i = 0; i < (dx < 0 ? -dx : dx); ++i) {
        actor->x += step_x;
        uint8_t flags = actor_flags_overlapping(actor);
        state |= actor_state_from_flags(flags);
        if (flags & PRG32_TILE_FLAG_SOLID) {
            actor->x -= step_x;
            actor->vx = 0;
            state |= step_x < 0
                ? PRG32_PLATFORM_HIT_LEFT
                : PRG32_PLATFORM_HIT_RIGHT;
            break;
        }
    }

    for (int i = 0; i < (dy < 0 ? -dy : dy); ++i) {
        int previous_y = actor->y;
        actor->y += step_y;
        uint8_t flags = actor_flags_overlapping(actor);
        state |= actor_state_from_flags(flags);
        if ((flags & PRG32_TILE_FLAG_SOLID) ||
            (step_y > 0 && actor_hits_platform_floor(actor, previous_y))) {
            actor->y -= step_y;
            actor->vy = 0;
            state |= step_y < 0
                ? PRG32_PLATFORM_HIT_HEAD
                : PRG32_PLATFORM_ON_GROUND;
            break;
        }
    }

    uint8_t flags = actor_flags_overlapping(actor);
    state |= actor_state_from_flags(flags);

    actor->state = state;
    return state;
}

uint16_t prg32_platform_actor_step(prg32_platform_actor_t *actor,
                                   uint32_t input_mask,
                                   int move_speed,
                                   int jump_speed,
                                   int gravity,
                                   int max_fall) {
    if (!actor) {
        return 0;
    }

    actor->vx = 0;
    if (input_mask & PRG32_BTN_LEFT) {
        actor->vx -= move_speed;
    }
    if (input_mask & PRG32_BTN_RIGHT) {
        actor->vx += move_speed;
    }
    if ((input_mask & (PRG32_BTN_A | PRG32_BTN_UP)) &&
        (actor->state & PRG32_PLATFORM_ON_GROUND)) {
        actor->vy = jump_speed;
    }

    if (max_fall < 0) {
        max_fall = -max_fall;
    }
    actor->vy += gravity;
    actor->vy = clamp_int(actor->vy, -max_fall, max_fall);

    actor->state = 0;
    return prg32_platform_actor_move(actor, actor->vx, actor->vy);
}

void prg32_platform_camera_follow(const prg32_platform_actor_t *actor,
                                  int deadzone_x,
                                  int deadzone_y) {
    if (!actor) {
        return;
    }

    int camera_x = prg32_playfield_camera_x();
    int camera_y = prg32_playfield_camera_y();
    int max_x = PRG32_PLAYFIELD_COLS * PRG32_TILE_W - PRG32_GAME_W;
    int max_y = PRG32_PLAYFIELD_ROWS * PRG32_TILE_H - PRG32_GAME_H;

    deadzone_x = clamp_int(deadzone_x, 0, PRG32_GAME_W / 2);
    deadzone_y = clamp_int(deadzone_y, 0, PRG32_GAME_H / 2);

    int actor_left = actor->x - camera_x;
    int actor_right = actor_left + (int)actor->w;
    int actor_top = actor->y - camera_y;
    int actor_bottom = actor_top + (int)actor->h;

    if (actor_left < deadzone_x) {
        camera_x = actor->x - deadzone_x;
    } else if (actor_right > PRG32_GAME_W - deadzone_x) {
        camera_x = actor->x + (int)actor->w - (PRG32_GAME_W - deadzone_x);
    }

    if (actor_top < deadzone_y) {
        camera_y = actor->y - deadzone_y;
    } else if (actor_bottom > PRG32_GAME_H - deadzone_y) {
        camera_y = actor->y + (int)actor->h - (PRG32_GAME_H - deadzone_y);
    }

    prg32_playfield_camera(clamp_int(camera_x, 0, max_x),
                           clamp_int(camera_y, 0, max_y));
}
