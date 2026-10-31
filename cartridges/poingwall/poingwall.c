/* POING Wall: the POING ball on a wall of PRG32 screens.
 *
 * Every console that joins the room becomes one 320-pixel section of a single
 * horizontal wall. One ball bounces along the wall, leaving one screen and
 * entering the next, and every console draws the part of it on its own screen.
 *
 * All the shared information travels in the multiplayer snapshot. See
 * README.md for the meaning of its fields and docs/architecture.md for why the
 * consoles need no other agreement. */
#include "prg32.h"

#include <stdint.h>

#define W 320
#define H 200
#define HORIZON 118
#define BALL_RADIUS 30
#define FLOOR_Y (H - 18 - BALL_RADIUS)

/* A different signature is a different room: change it whenever the meaning of
 * the snapshot fields changes. */
#define ROOM_SIGNATURE "poing-wall-v2"

/* Snapshot flags. */
#define FLAG_READY (1u << 0)     /* Has watched the room long enough to lead. */
#define FLAG_AUTHORITY (1u << 1) /* Moves the ball; x, y, sprite are the ball. */
#define FLAG_ROSTER_SHIFT 8      /* Bits 8-15: screens this console can see. */

/* A joining console watches this long before it may lead, so that it can take
 * over the ball the room already has instead of starting another one. */
#define SETTLE_MS 700u

/* The simulation advances in fixed steps, so the ball moves at the same speed
 * on a fast computer and on the board. Positions are in 1/16 pixel. */
#define STEP_MS 33u
#define MAX_STEPS_PER_FRAME 8u
#define SUBPIXEL 16
#define BALL_SPEED_X 56     /* 3.5 pixels per step, or 106 pixels per second */
#define BALL_BOUNCE_VY (-96)
#define BALL_GRAVITY 4

#define MAX_SCREENS (PRG32_MP_MAX_PEERS + 1)
#define NOTE_CHANNEL 0

enum {
    IDX_BACKGROUND = 16,
    IDX_FLOOR,
    IDX_GRID,
    IDX_WALL_END,
    IDX_SHADOW,
    IDX_STAR_DIM,
    IDX_STAR_BRIGHT,
    IDX_HIGHLIGHT,
    IDX_SHADE_BASE = 32
};

typedef struct {
    int32_t x; /* 1/16 pixel from the left end of the wall */
    int32_t y; /* 1/16 pixel from the top of the screen */
    int16_t vx;
    int16_t vy;
} ball_t;

typedef struct {
    ball_t ball;
    uint32_t ids[MAX_SCREENS];
    int screen_count;
    int screen;
    uint32_t local_id;
    uint32_t joined_ms;
    uint32_t last_step_ms;
    uint32_t leader_frame;
    uint32_t last_input_any;
    uint8_t network;
    uint8_t ready;
    uint8_t authority;
    uint8_t have_leader_frame;
} wall_state_t;

static wall_state_t s;

static int clampi(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static uint32_t isqrt32(uint32_t n) {
    uint32_t bit = 1u << 30;
    uint32_t root = 0;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3));
}

static void rect_clip(int x, int y, int w, int h, uint8_t index) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w > 0 && h > 0) prg32_gfx_rect_indexed(x, y, w, h, index);
}

static uint16_t shade_rgb565(int family, int light) {
    static const uint8_t base[3][3] = {
        {26, 210, 244}, {244, 52, 108}, {238, 244, 255}
    };
    int r = (base[family][0] * light) >> 8;
    int g = (base[family][1] * light) >> 8;
    int b = (base[family][2] * light) >> 8;
    return rgb565(r, g, b);
}

static void init_palette(void) {
    prg32_palette_set(IDX_BACKGROUND, rgb565(2, 4, 12));
    prg32_palette_set(IDX_FLOOR, rgb565(5, 8, 18));
    prg32_palette_set(IDX_GRID, rgb565(28, 45, 75));
    prg32_palette_set(IDX_WALL_END, rgb565(244, 52, 108));
    prg32_palette_set(IDX_SHADOW, rgb565(2, 4, 10));
    prg32_palette_set(IDX_STAR_DIM, rgb565(35, 72, 105));
    prg32_palette_set(IDX_STAR_BRIGHT, rgb565(180, 240, 255));
    prg32_palette_set(IDX_HIGHLIGHT, rgb565(215, 255, 255));
    for (int family = 0; family < 3; ++family)
        for (int level = 0; level < 8; ++level)
            prg32_palette_set((uint8_t)(IDX_SHADE_BASE + family * 8 + level),
                              shade_rgb565(family, level * 32));
}

/* ---------- The ball ---------- */

static int32_t wall_width(int screens) {
    return (int32_t)(screens > 0 ? screens : 1) * W * SUBPIXEL;
}

/* Keeps the ball on a wall that may just have become shorter. */
static void ball_keep_on_wall(ball_t *ball, int screens) {
    int32_t right = wall_width(screens) - BALL_RADIUS * SUBPIXEL;
    if (ball->x > right) ball->x = right;
    if (ball->x < BALL_RADIUS * SUBPIXEL) ball->x = BALL_RADIUS * SUBPIXEL;
}

/* One simulation step. Returns nonzero when the ball hit something. */
static int ball_step(ball_t *ball, int screens) {
    int hit = 0;
    int32_t left = BALL_RADIUS * SUBPIXEL;
    int32_t right = wall_width(screens) - BALL_RADIUS * SUBPIXEL;
    int32_t floor_y = FLOOR_Y * SUBPIXEL;

    ball->x += ball->vx;
    if (ball->x <= left) {
        ball->x = left;
        ball->vx = BALL_SPEED_X;
        hit = 1;
    } else if (ball->x >= right) {
        ball->x = right;
        ball->vx = -BALL_SPEED_X;
        hit = 1;
    }

    ball->vy = (int16_t)(ball->vy + BALL_GRAVITY);
    ball->y += ball->vy;
    if (ball->y >= floor_y) {
        ball->y = floor_y;
        ball->vy = BALL_BOUNCE_VY;
        hit = 1;
    }
    return hit;
}

/* ---------- The snapshot ---------- */

/* x is in whole pixels: nine screens fit int16_t only that way. y keeps its
 * fraction. sprite packs both velocities, which fit a signed byte. */
static void ball_publish(const ball_t *ball, uint16_t flags) {
    uint16_t sprite = (uint16_t)(((uint16_t)(uint8_t)(int8_t)ball->vy << 8) |
                                 (uint8_t)(int8_t)ball->vx);
    prg32_multiplayer_set_local_state((int16_t)(ball->x / SUBPIXEL),
                                      (int16_t)ball->y, sprite, flags);
}

static void ball_from_snapshot(ball_t *ball, const prg32_player_state_t *peer) {
    ball->x = (int32_t)peer->x * SUBPIXEL;
    ball->y = peer->y;
    ball->vx = (int8_t)(peer->sprite & 0xffu);
    ball->vy = (int8_t)(peer->sprite >> 8);
}

/* ---------- The room ---------- */

/* The same set of IDs sorts the same way on every console, so all of them agree
 * on the order of the screens without exchanging it. */
static void sort_ids(uint32_t *ids, int count) {
    for (int i = 1; i < count; ++i) {
        uint32_t value = ids[i];
        int j = i;
        while (j > 0 && ids[j - 1] > value) {
            ids[j] = ids[j - 1];
            --j;
        }
        ids[j] = value;
    }
}

/* Reads the room, then decides who moves the ball: the lowest ID among the
 * consoles that are ready. Follows that console's snapshot when it is another. */
static void update_room(uint32_t now) {
    uint32_t input = 0;
    prg32_player_state_t leader;
    int have_leader = 0;
    uint32_t lowest_ready = s.ready ? s.local_id : 0;
    int count = 0;
    int peers = prg32_multiplayer_get_peer_count();

    if (s.local_id) s.ids[count++] = s.local_id;
    for (int i = 0; i < peers && count < MAX_SCREENS; ++i) {
        prg32_player_state_t peer;
        if (prg32_multiplayer_get_peer(i, &peer) != 0) continue;
        s.ids[count++] = peer.player_id;
        input |= peer.input;
        if ((peer.flags & FLAG_READY) &&
            (lowest_ready == 0 || peer.player_id < lowest_ready)) {
            lowest_ready = peer.player_id;
        }
        if ((peer.flags & FLAG_AUTHORITY) &&
            (!have_leader || peer.player_id < leader.player_id)) {
            leader = peer;
            have_leader = 1;
        }
    }
    sort_ids(s.ids, count);
    s.screen_count = count > 0 ? count : 1;
    s.screen = 0;
    for (int i = 0; i < count; ++i) {
        if (s.ids[i] == s.local_id) s.screen = i;
    }

    s.authority = s.ready && lowest_ready == s.local_id;
    if (!s.authority && have_leader) {
        /* frame moves forward whenever the leader publishes something new. A
         * new leader has a new counter, and any leader's numbers are welcome. */
        if (!s.have_leader_frame || leader.frame != s.leader_frame) {
            ball_from_snapshot(&s.ball, &leader);
            s.leader_frame = leader.frame;
            s.have_leader_frame = 1;
            s.last_step_ms = now;
        }
    }
    if (s.authority) s.have_leader_frame = 0;

    /* Any player's A button sends the ball back the other way. */
    if (s.authority) {
        uint32_t any = input | prg32_input_read();
        if ((any & ~s.last_input_any) & PRG32_BTN_A) s.ball.vx = (int16_t)-s.ball.vx;
        s.last_input_any = any;
    }
}

static void note_for_hit(void) {
    int32_t local = s.ball.x / SUBPIXEL - s.screen * W;
    if (local > -BALL_RADIUS && local < W + BALL_RADIUS) {
        prg32_audio_note(NOTE_CHANNEL, 0, 48, 255, 90);
    }
}

void poingwall_init(void) {
    s.ball.x = (W / 4) * SUBPIXEL;
    s.ball.y = (FLOOR_Y - 60) * SUBPIXEL;
    s.ball.vx = BALL_SPEED_X;
    s.ball.vy = 0;
    s.screen_count = 1;
    s.screen = 0;
    s.local_id = 0;
    s.joined_ms = 0;
    s.ready = 0;
    s.authority = 0;
    s.have_leader_frame = 0;
    s.leader_frame = 0;
    s.last_input_any = 0;
    s.last_step_ms = prg32_ticks_ms();
    init_palette();
    prg32_multiplayer_init();
    s.network = prg32_multiplayer_available() &&
                prg32_multiplayer_join(ROOM_SIGNATURE, PRG32_MP_FLAG_ENABLE) == 0;
}

void poingwall_update(void) {
    uint32_t now = prg32_ticks_ms();
    uint32_t input = prg32_input_read();
    uint32_t steps = 0;

    if (s.network) {
        uint32_t id = prg32_multiplayer_local_player_id();
        if (id != s.local_id) {
            /* Admitted, or admitted again after a reconnection. */
            s.local_id = id;
            s.ready = 0;
            s.joined_ms = now;
        }
        if (s.local_id && !s.ready && (uint32_t)(now - s.joined_ms) >= SETTLE_MS) {
            s.ready = 1;
        }
        prg32_multiplayer_set_input(input & 0x7fu);
        update_room(now);
    } else {
        /* No server: the wall is this one screen. */
        s.authority = 1;
        s.screen_count = 1;
        s.screen = 0;
    }
    ball_keep_on_wall(&s.ball, s.screen_count);

    /* A follower predicts with the same steps between snapshots, so the ball
     * keeps moving smoothly at a frame rate above the 20 Hz of the network. */
    while ((uint32_t)(now - s.last_step_ms) >= STEP_MS && steps < MAX_STEPS_PER_FRAME) {
        s.last_step_ms += STEP_MS;
        ++steps;
        if (ball_step(&s.ball, s.screen_count)) note_for_hit();
    }
    if ((uint32_t)(now - s.last_step_ms) >= STEP_MS) s.last_step_ms = now;

    if (s.network) {
        uint16_t flags = (uint16_t)((uint32_t)s.screen_count << FLAG_ROSTER_SHIFT);
        if (s.ready) flags |= FLAG_READY;
        if (s.authority) flags |= FLAG_AUTHORITY;
        ball_publish(&s.ball, flags);
        prg32_multiplayer_tick();
    }
}

/* ---------- Drawing ---------- */

static void text_number(char *out, int value) {
    /* One or two digits: at most nine screens. */
    if (value >= 10) *out++ = (char)('0' + value / 10);
    *out++ = (char)('0' + value % 10);
    *out = '\0';
}

static void draw_floor(void) {
    /* One vanishing point at the middle of the whole wall, so the floor lines
     * run on from one screen into the next. */
    int vanish = s.screen_count * W / 2 - s.screen * W;
    int last_line = s.screen_count * W + W;
    prg32_gfx_rect_indexed(0, HORIZON, W, H - HORIZON, IDX_FLOOR);
    for (int y = HORIZON + 3; y < H; ++y) {
        if (((720 / (y - HORIZON)) & 7) == 0) {
            prg32_gfx_rect_indexed(0, y, W, 1, IDX_GRID);
        }
    }
    for (int line = -W; line <= last_line; line += 40) {
        int bottom = line - s.screen * W;
        int last_x = vanish;
        for (int y = HORIZON; y < H; y += 2) {
            int x = vanish + ((bottom - vanish) * (y - HORIZON)) / (H - HORIZON);
            int x0 = last_x < x ? last_x : x;
            int width = last_x < x ? x - last_x + 1 : last_x - x + 1;
            rect_clip(x0, y, width, 2, IDX_GRID);
            last_x = x;
        }
    }
}

/* The checker turns with the position on the wall, so the ball looks the same
 * on both sides of a screen boundary. */
static void draw_ball(int cx, int cy, int radius, int rotation) {
    int r2 = radius * radius;
    for (int py = -radius; py <= radius; py += 2) {
        int xr = (int)isqrt32((uint32_t)(r2 - py * py));
        int run_x = cx - xr;
        uint8_t run_index = 0;
        int have_run = 0;
        for (int px = -xr; px <= xr; px += 2) {
            int z = (int)isqrt32((uint32_t)(r2 - py * py - px * px));
            int u = rotation + ((px * 96) / (z + radius + 1));
            int v = py + 24;
            int family = ((u >> 4) ^ (v >> 4)) & 1;
            int light = clampi(120 + ((-px - py + (z << 1)) * 90) / (radius * 4), 58, 255);
            uint8_t index = (uint8_t)(IDX_SHADE_BASE + family * 8 + clampi(light >> 5, 0, 7));
            if (!have_run) {
                run_x = cx + px;
                run_index = index;
                have_run = 1;
            } else if (index != run_index) {
                rect_clip(run_x, cy + py, cx + px - run_x, 2, run_index);
                run_x = cx + px;
                run_index = index;
            }
        }
        if (have_run) rect_clip(run_x, cy + py, cx + xr - run_x + 1, 2, run_index);
    }
    rect_clip(cx - radius / 3, cy - radius + 4, radius / 3, 2, IDX_HIGHLIGHT);
}

static void draw_hud(void) {
    uint16_t bright = prg32_palette_get(IDX_STAR_BRIGHT);
    uint16_t back = prg32_palette_get(IDX_BACKGROUND);
    char number[4];

    prg32_gfx_text8(6, 5, "POING WALL", bright, back);
    if (!s.network) {
        prg32_gfx_text8(6, 187, "NO SERVER: THIS SCREEN ONLY", bright, back);
        return;
    }
    if (!s.local_id) {
        prg32_gfx_text8(6, 187, "JOINING THE ROOM", bright, back);
        return;
    }
    prg32_gfx_text8(6, 187, "SCREEN", bright, back);
    text_number(number, s.screen + 1);
    prg32_gfx_text8(62, 187, number, bright, back);
    prg32_gfx_text8(82, 187, "OF", bright, back);
    text_number(number, s.screen_count);
    prg32_gfx_text8(102, 187, number, bright, back);
    prg32_gfx_text8(232, 187, s.authority ? "LEADS BALL" : "FOLLOWS", bright, back);
}

void poingwall_draw(void) {
    int cx = (int)(s.ball.x / SUBPIXEL) - s.screen * W;
    int cy = (int)(s.ball.y / SUBPIXEL);

    prg32_gfx_clear_indexed(IDX_BACKGROUND);
    for (int i = 0; i < 40; ++i) {
        /* The screen number moves the stars, so each screen has its own sky. */
        int seed = i + s.screen * 41;
        int x = (seed * 73 + 19) % W;
        int y = (seed * 37 + 11) % (HORIZON - 10);
        prg32_gfx_pixel_indexed(x, y, (i & 7) == 0 ? IDX_STAR_BRIGHT : IDX_STAR_DIM);
    }
    draw_floor();

    /* The two ends of the wall, which the ball bounces off. Between them it
     * passes straight through the edges of the screens. */
    if (s.screen == 0) rect_clip(0, 0, 3, H, IDX_WALL_END);
    if (s.screen == s.screen_count - 1) rect_clip(W - 3, 0, 3, H, IDX_WALL_END);

    if (cx > -BALL_RADIUS - 8 && cx < W + BALL_RADIUS + 8) {
        for (int dy = -4; dy <= 4; ++dy) {
            int half = (int)isqrt32((uint32_t)(16 - dy * dy)) * BALL_RADIUS / 5;
            rect_clip(cx - half, FLOOR_Y + BALL_RADIUS + 3 + dy, half * 2 + 1, 1, IDX_SHADOW);
        }
        draw_ball(cx, cy, BALL_RADIUS, (int)(s.ball.x / SUBPIXEL) * 2);
    }
    draw_hud();
}
