#include "prg32.h"

#include <stdint.h>

#define W 320
#define H 200
#define FLOOR_Y 142
#define PERF_FRAMES 300u

enum {
    IDX_BACKGROUND = 16,
    IDX_FLOOR,
    IDX_GRID_HORIZONTAL,
    IDX_GRID_PERSPECTIVE,
    IDX_SHADOW_A,
    IDX_SHADOW_B,
    IDX_STAR_DIM,
    IDX_STAR_BRIGHT,
    IDX_HUD,
    IDX_HUD_ACCENT,
    IDX_HIGHLIGHT,
    IDX_SHADE_BASE = 32
};

typedef struct {
    int yaw;
    int elevation;
    int zoom;
    int spin;
    int paused;
    int show_hud;
    uint32_t last_input;
    uint32_t frame;
    uint32_t perf_frame;
    int perf_active;
    int perf_done;
    uint64_t update_start;
    uint64_t update_end;
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

static uint8_t shade_index(int family, int light) {
    return (uint8_t)(IDX_SHADE_BASE + family * 8 + clampi(light >> 5, 0, 7));
}

static void init_palette(void) {
    prg32_palette_set(IDX_BACKGROUND, rgb565(2, 4, 12));
    prg32_palette_set(IDX_FLOOR, rgb565(5, 8, 18));
    prg32_palette_set(IDX_GRID_HORIZONTAL, rgb565(28, 45, 75));
    prg32_palette_set(IDX_GRID_PERSPECTIVE, rgb565(18, 34, 62));
    prg32_palette_set(IDX_SHADOW_A, rgb565(2, 4, 10));
    prg32_palette_set(IDX_SHADOW_B, rgb565(3, 4, 10));
    prg32_palette_set(IDX_STAR_DIM, rgb565(35, 72, 105));
    prg32_palette_set(IDX_STAR_BRIGHT, rgb565(180, 240, 255));
    prg32_palette_set(IDX_HUD, rgb565(210, 250, 255));
    prg32_palette_set(IDX_HUD_ACCENT, rgb565(100, 220, 245));
    prg32_palette_set(IDX_HIGHLIGHT, rgb565(215, 255, 255));
    for (int family = 0; family < 3; ++family)
        for (int level = 0; level < 8; ++level)
            prg32_palette_set((uint8_t)(IDX_SHADE_BASE + family * 8 + level),
                              shade_rgb565(family, level * 32));
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
    prg32_gfx_rect_indexed(0, horizon, W, H - horizon, IDX_FLOOR);
    for (y = horizon + 3; y < H; ++y) {
        int d = y - horizon;
        if ((((720 / d) + (int)(s.frame >> 1)) & 7) == 0) {
            prg32_gfx_rect_indexed(0, y, W, 1, IDX_GRID_HORIZONTAL);
        }
    }
    for (int line = -9; line <= 9; ++line) {
        int bottom = 160 + line * 30 + (yaw >> 2);
        int last_x = W / 2;
        for (y = horizon; y < H; y += 2) {
            int x = W / 2 + ((bottom - W / 2) * (y - horizon)) / (H - horizon);
            int x0 = last_x < x ? last_x : x;
            int width = last_x < x ? x - last_x + 1 : last_x - x + 1;
            rect_clip(x0, y, width, 2, IDX_GRID_PERSPECTIVE);
            last_x = x;
        }
    }
}

static void draw_shadow(int cx, int y, int radius) {
    for (int dy = -7; dy <= 7; ++dy) {
        int half = (int)isqrt32((uint32_t)(49 - dy * dy)) * radius / 13;
        prg32_gfx_rect_indexed(cx - half, y + dy, half * 2 + 1, 1,
                              (dy & 1) ? IDX_SHADOW_B : IDX_SHADOW_A);
    }
}

static void draw_ball(int cx, int cy, int radius, int rotation) {
    int r2 = radius * radius;
    /* Two-line bands and quantized lighting preserve the effect while keeping
       public-ABI draw-call pressure practical under instruction emulation. */
    for (int py = -radius; py <= radius; py += 2) {
        int xr = (int)isqrt32((uint32_t)(r2 - py * py));
        int run_x = cx - xr;
        uint8_t run_index = 0;
        int have_run = 0;
        for (int px = -xr; px <= xr; px += 2) {
            int z = (int)isqrt32((uint32_t)(r2 - py * py - px * px));
            int u = rotation + ((px * 96) / (z + radius + 1));
            int v = py + ((s.elevation * z) >> 7) + 24;
            int checker = (((u >> 4) ^ (v >> 4)) & 1);
            int family = logo_bit(u + 38, v) ? 2 : checker;
            int light = clampi(120 + ((-px - py + (z << 1)) * 90) / (radius * 4), 58, 255);
            light &= ~31;
            uint8_t index = shade_index(family, light);
            if (!have_run) {
                run_x = cx + px;
                run_index = index;
                have_run = 1;
            } else if (index != run_index) {
                prg32_gfx_rect_indexed(run_x, cy + py, cx + px - run_x, 2, run_index);
                run_x = cx + px;
                run_index = index;
            }
        }
        if (have_run) prg32_gfx_rect_indexed(run_x, cy + py, cx + xr - run_x + 1, 2, run_index);
    }
    prg32_gfx_rect_indexed(cx - radius / 3, cy - radius + 4, radius / 3, 2, IDX_HIGHLIGHT);
}

static void start_performance_test(void) {
    prg32_perf_suite_desc_t suite;
    prg32_perf_case_desc_t test_case;
    suite.abi_version = PRG32_PERF_ABI_VERSION;
    suite.struct_size = sizeof(suite);
    suite.suite_version = 1u;
    suite.name = "poing-indexed-renderer";
    test_case.abi_version = PRG32_PERF_ABI_VERSION;
    test_case.struct_size = sizeof(test_case);
    test_case.case_index = 0u;
    test_case.color_mode = PRG32_PERF_COLOR_INDEXED;
    test_case.name = "poing-gameplay";
    test_case.metric_goal = "full indexed procedural gameplay frame";
    s.perf_frame = 0;
    s.perf_done = 0;
    s.perf_active = prg32_perf_begin(&suite) == 0 &&
                    prg32_perf_case_begin(&test_case, PERF_FRAMES) == 0;
    if (!s.perf_active) prg32_perf_abort();
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
    s.perf_active = 0;
    s.perf_done = 0;
    init_palette();
    prg32_audio_note(0, 0, 48, 255, 90);
}

void poing_update(void) {
    s.update_start = prg32_perf_now_us();
    uint32_t input = prg32_input_read();
    uint32_t pressed = input & ~s.last_input;
    if (input & PRG32_BTN_LEFT) s.yaw = clampi(s.yaw - 2, -50, 50);
    if (input & PRG32_BTN_RIGHT) s.yaw = clampi(s.yaw + 2, -50, 50);
    if (input & PRG32_BTN_UP) s.elevation = clampi(s.elevation - 2, -56, 48);
    if (input & PRG32_BTN_DOWN) s.elevation = clampi(s.elevation + 2, -56, 48);
    if (pressed & PRG32_BTN_A) s.zoom = (s.zoom + 1) % 3;
    if (pressed & PRG32_BTN_B) s.paused = !s.paused;
    if (pressed & PRG32_BTN_SELECT) {
        if (s.perf_active) { prg32_perf_abort(); s.perf_active = 0; }
        else start_performance_test();
    }
    if (!s.paused) {
        uint8_t phase = (uint8_t)(s.frame * 3u);
        s.spin += 3;
        ++s.frame;
        if ((uint8_t)(s.frame * 3u) < phase) prg32_audio_note(0, 0, 48, 255, 90);
    }
    s.last_input = input;
    s.update_end = prg32_perf_now_us();
}

void poing_draw(void) {
    uint64_t draw_start = prg32_perf_now_us();
    int radius = 43 + s.zoom * 7;
    int horizon = clampi(FLOOR_Y + (s.elevation >> 2), 126, 154);
    int bounce = (isin8((uint8_t)(s.frame * 3)) + 256) * 25 / 512;
    int cx = W / 2 + s.yaw;
    int cy = horizon - radius - 4 - bounce;
    prg32_gfx_clear_indexed(IDX_BACKGROUND);
    for (int i = 0; i < 54; ++i) {
        int x = (i * 73 + 19) % W;
        int y = (i * 37 + 11) % (horizon - 10);
        int twinkle = ((i + (int)(s.frame >> 3)) & 7) == 0;
        prg32_gfx_pixel_indexed(x, y, twinkle ? IDX_STAR_BRIGHT : IDX_STAR_DIM);
    }
    draw_floor(horizon, s.yaw);
    draw_shadow(cx, horizon + 12, radius);
    draw_ball(cx, cy, radius, s.spin + s.yaw);
    if (s.show_hud) {
        prg32_gfx_text8(6, 5, "POING / REAL-TIME PRG32",
                        prg32_palette_get(IDX_HUD), prg32_palette_get(IDX_BACKGROUND));
        prg32_gfx_text8(6, 187, s.perf_active ? "PERF RECORDING  SELECT ABORT" :
                        (s.perf_done ? "PERF COMPLETE  API RESULT READY" :
                         "STICK VIEW A ZOOM B FREEZE SEL PERF"),
                        prg32_palette_get(IDX_HUD_ACCENT),
                        prg32_palette_get(IDX_BACKGROUND));
    }
    if (s.perf_active) {
        uint64_t draw_end = prg32_perf_now_us();
        uint64_t present_start = prg32_perf_now_us();
        prg32_gfx_present();
        uint64_t present_end = prg32_perf_now_us();
        prg32_perf_sample_t sample;
        sample.abi_version = PRG32_PERF_ABI_VERSION;
        sample.struct_size = sizeof(sample);
        sample.frame_index = s.perf_frame;
        sample.update_us = (uint32_t)(s.update_end - s.update_start);
        sample.draw_us = (uint32_t)(draw_end - draw_start);
        sample.present_us = (uint32_t)(present_end - present_start);
        sample.frame_total_us = (uint32_t)(present_end - s.update_start);
        sample.input_mask = s.last_input;
        if (prg32_perf_record(&sample) != 0) {
            prg32_perf_abort(); s.perf_active = 0;
        } else if (++s.perf_frame == PERF_FRAMES) {
            s.perf_done = prg32_perf_case_end() == 0 && prg32_perf_end() == 0;
            if (!s.perf_done) prg32_perf_abort();
            s.perf_active = 0;
        }
    }
}
