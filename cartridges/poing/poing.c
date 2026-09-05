#include "prg32.h"

#include <stdint.h>

#define W 320
#define H 200
#define FLOOR_Y 142

typedef struct {
    int yaw;
    int elevation;
    int zoom;
    int spin;
    int paused;
    int show_hud;
    uint32_t last_input;
    uint32_t frame;
} poing_state_t;

static poing_state_t s;

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

/* 256-step, integer-only sine. Output is approximately -256..+256. */
static int isin8(uint8_t phase) {
    int x = phase;
    int sign = 1;
    if (x >= 128) { x -= 128; sign = -1; }
    if (x > 64) x = 128 - x;
    return sign * ((x * (128 - x)) >> 4);
}

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3));
}

static void rect_clip(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w > 0 && h > 0) prg32_gfx_rect(x, y, w, h, color);
}

static uint16_t shade(int family, int light) {
    static const uint8_t base[3][3] = {
        {26, 210, 244}, {244, 52, 108}, {238, 244, 255}
    };
    int r = (base[family][0] * light) >> 8;
    int g = (base[family][1] * light) >> 8;
    int b = (base[family][2] * light) >> 8;
    return rgb565(r, g, b);
}

/* A compact 3x5 block wordmark, sampled as a repeating spherical texture. */
static int logo_bit(int u, int v) {
    static const char glyph[5][20] = {
        "1110110011101110111",
        "1010101010000010001",
        "1110110010101110111",
        "1000101010100010100",
        "1000101011101110111"
    };
    int wrapped = (u + 64) & 127;
    int x;
    int y;
    wrapped -= 64;
    x = (wrapped + 38) / 4;
    y = (v + 10) / 4;
    if (x < 0 || x >= 19 || y < 0 || y >= 5) return 0;
    return glyph[y][x] == '1';
}

static void draw_floor(int horizon, int yaw) {
    int y;
    prg32_gfx_rect(0, horizon, W, H - horizon, rgb565(5, 8, 18));
    for (y = horizon + 3; y < H; ++y) {
        int d = y - horizon;
        if ((((720 / d) + (int)(s.frame >> 1)) & 7) == 0) {
            prg32_gfx_rect(0, y, W, 1, rgb565(28, 45, 75));
        }
    }
    for (int line = -9; line <= 9; ++line) {
        int bottom = 160 + line * 30 + (yaw >> 2);
        int last_x = W / 2;
        for (y = horizon; y < H; y += 2) {
            int x = W / 2 + ((bottom - W / 2) * (y - horizon)) / (H - horizon);
            int x0 = last_x < x ? last_x : x;
            int width = last_x < x ? x - last_x + 1 : last_x - x + 1;
            rect_clip(x0, y, width, 2, rgb565(18, 34, 62));
            last_x = x;
        }
    }
}

static void draw_shadow(int cx, int y, int radius) {
    for (int dy = -7; dy <= 7; ++dy) {
        int half = (int)isqrt32((uint32_t)(49 - dy * dy)) * radius / 13;
        prg32_gfx_rect(cx - half, y + dy, half * 2 + 1, 1,
                       rgb565(2 + (dy & 1), 4, 10));
    }
}

static void draw_ball(int cx, int cy, int radius, int rotation) {
    int r2 = radius * radius;
    /* Two-line bands and quantized lighting preserve the effect while keeping
       public-ABI draw-call pressure practical under instruction emulation. */
    for (int py = -radius; py <= radius; py += 2) {
        int xr = (int)isqrt32((uint32_t)(r2 - py * py));
        int run_x = cx - xr;
        uint16_t run_color = 0;
        int have_run = 0;
        for (int px = -xr; px <= xr; px += 2) {
            int z = (int)isqrt32((uint32_t)(r2 - py * py - px * px));
            int u = rotation + ((px * 96) / (z + radius + 1));
            int v = py + ((s.elevation * z) >> 7) + 24;
            int checker = (((u >> 4) ^ (v >> 4)) & 1);
            int family = logo_bit(u + 38, v) ? 2 : checker;
            int light = clampi(120 + ((-px - py + (z << 1)) * 90) / (radius * 4), 58, 255);
            light &= ~31;
            uint16_t color = shade(family, light);
            if (!have_run) {
                run_x = cx + px;
                run_color = color;
                have_run = 1;
            } else if (color != run_color) {
                prg32_gfx_rect(run_x, cy + py, cx + px - run_x, 2, run_color);
                run_x = cx + px;
                run_color = color;
            }
        }
        if (have_run) prg32_gfx_rect(run_x, cy + py, cx + xr - run_x + 1, 2, run_color);
    }
    prg32_gfx_rect(cx - radius / 3, cy - radius + 4, radius / 3, 2, rgb565(215, 255, 255));
}

void poing_init(void) {
    s.yaw = 0;
    s.elevation = -18;
    s.zoom = 0;
    s.spin = 0;
    s.paused = 0;
    s.show_hud = 1;
    s.last_input = 0;
    s.frame = 0;
    prg32_audio_note(48, 90);
}

void poing_update(void) {
    uint32_t input = prg32_input_read();
    uint32_t pressed = input & ~s.last_input;
    if (input & PRG32_BTN_LEFT) s.yaw = clampi(s.yaw - 2, -50, 50);
    if (input & PRG32_BTN_RIGHT) s.yaw = clampi(s.yaw + 2, -50, 50);
    if (input & PRG32_BTN_UP) s.elevation = clampi(s.elevation - 2, -56, 48);
    if (input & PRG32_BTN_DOWN) s.elevation = clampi(s.elevation + 2, -56, 48);
    if (pressed & PRG32_BTN_A) s.zoom = (s.zoom + 1) % 3;
    if (pressed & PRG32_BTN_B) s.paused = !s.paused;
    if (pressed & PRG32_BTN_SELECT) s.show_hud = !s.show_hud;
    if (!s.paused) {
        uint8_t phase = (uint8_t)(s.frame * 3u);
        s.spin += 3;
        ++s.frame;
        if ((uint8_t)(s.frame * 3u) < phase) prg32_audio_note(48, 90);
    }
    s.last_input = input;
}

void poing_draw(void) {
    int radius = 43 + s.zoom * 7;
    int horizon = clampi(FLOOR_Y + (s.elevation >> 2), 126, 154);
    int bounce = (isin8((uint8_t)(s.frame * 3)) + 256) * 25 / 512;
    int cx = W / 2 + s.yaw;
    int cy = horizon - radius - 4 - bounce;
    prg32_gfx_clear(rgb565(2, 4, 12));
    for (int i = 0; i < 54; ++i) {
        int x = (i * 73 + 19) % W;
        int y = (i * 37 + 11) % (horizon - 10);
        int twinkle = ((i + (int)(s.frame >> 3)) & 7) == 0;
        prg32_gfx_pixel(x, y, twinkle ? rgb565(180, 240, 255) : rgb565(35, 72, 105));
    }
    draw_floor(horizon, s.yaw);
    draw_shadow(cx, horizon + 12, radius);
    draw_ball(cx, cy, radius, s.spin + s.yaw);
    if (s.show_hud) {
        prg32_gfx_text8(6, 5, "POING / REAL-TIME PRG32", rgb565(210, 250, 255), rgb565(2, 4, 12));
        prg32_gfx_text8(6, 187, "STICK VIEW  A ZOOM  B FREEZE", rgb565(100, 220, 245), rgb565(2, 4, 12));
    }
}
