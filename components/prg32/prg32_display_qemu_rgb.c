#include "prg32.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "prg32_qemu";

#define PRG32_VIEWPORT_Y ((PRG32_LCD_H - PRG32_GAME_H) / 2)

static esp_lcd_panel_handle_t g_panel;
static int g_fullscreen;
static int g_band_color_set;
static int g_band_area_valid;
static uint16_t g_band_color;
static uint16_t g_last_band_color;
static uint16_t g_fb[PRG32_LCD_W * PRG32_LCD_H];
static char g_band_text_cache[2][80];
static uint16_t g_band_fg_cache[2];
static uint16_t g_band_bg_cache[2];
static int g_band_cache_valid[2];
static int g_dirty_x0, g_dirty_y0, g_dirty_x1, g_dirty_y1;
#define PRG32_FLASH_RODATA __attribute__((section(".rodata")))

void prg32_band_note_frame(uint32_t now_ms);
int prg32_band_visible(uint8_t band);
uint16_t prg32_band_fg(uint8_t band);
uint16_t prg32_band_bg(uint8_t band, uint16_t fallback);
const char *prg32_band_render_text(uint8_t band, uint32_t now_ms);
void prg32_gfx_lock_init(void);

static const uint8_t g_font8[96][8] PRG32_FLASH_RODATA = {
    [' ' - 32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!' - 32] = {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    ['"' - 32] = {0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00},
    ['#' - 32] = {0x24,0x7e,0x24,0x24,0x7e,0x24,0x00,0x00},
    ['$' - 32] = {0x18,0x3e,0x58,0x3c,0x1a,0x7c,0x18,0x00},
    ['%' - 32] = {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00},
    ['&' - 32] = {0x30,0x48,0x30,0x4a,0x44,0x3a,0x00,0x00},
    ['\'' - 32] = {0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00},
    ['(' - 32] = {0x0c,0x10,0x20,0x20,0x20,0x10,0x0c,0x00},
    [')' - 32] = {0x30,0x08,0x04,0x04,0x04,0x08,0x30,0x00},
    ['*' - 32] = {0x00,0x24,0x18,0x7e,0x18,0x24,0x00,0x00},
    ['+' - 32] = {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00},
    [',' - 32] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10},
    ['-' - 32] = {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00},
    ['.' - 32] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['/' - 32] = {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
    ['0' - 32] = {0x3c,0x42,0x46,0x4a,0x52,0x62,0x3c,0x00},
    ['1' - 32] = {0x18,0x28,0x08,0x08,0x08,0x08,0x3e,0x00},
    ['2' - 32] = {0x3c,0x42,0x02,0x0c,0x30,0x40,0x7e,0x00},
    ['3' - 32] = {0x3c,0x42,0x02,0x1c,0x02,0x42,0x3c,0x00},
    ['4' - 32] = {0x0c,0x14,0x24,0x44,0x7e,0x04,0x04,0x00},
    ['5' - 32] = {0x7e,0x40,0x7c,0x02,0x02,0x42,0x3c,0x00},
    ['6' - 32] = {0x1c,0x20,0x40,0x7c,0x42,0x42,0x3c,0x00},
    ['7' - 32] = {0x7e,0x02,0x04,0x08,0x10,0x10,0x10,0x00},
    ['8' - 32] = {0x3c,0x42,0x42,0x3c,0x42,0x42,0x3c,0x00},
    ['9' - 32] = {0x3c,0x42,0x42,0x3e,0x02,0x04,0x38,0x00},
    [':' - 32] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    [';' - 32] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x10},
    ['<' - 32] = {0x0c,0x10,0x20,0x40,0x20,0x10,0x0c,0x00},
    ['=' - 32] = {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00},
    ['>' - 32] = {0x30,0x08,0x04,0x02,0x04,0x08,0x30,0x00},
    ['?' - 32] = {0x3c,0x42,0x02,0x0c,0x10,0x00,0x10,0x00},
    ['@' - 32] = {0x3c,0x42,0x5a,0x56,0x5e,0x40,0x3c,0x00},
    ['A' - 32] = {0x18,0x24,0x42,0x42,0x7e,0x42,0x42,0x00},
    ['B' - 32] = {0x7c,0x42,0x42,0x7c,0x42,0x42,0x7c,0x00},
    ['C' - 32] = {0x3c,0x42,0x40,0x40,0x40,0x42,0x3c,0x00},
    ['D' - 32] = {0x78,0x44,0x42,0x42,0x42,0x44,0x78,0x00},
    ['E' - 32] = {0x7e,0x40,0x40,0x78,0x40,0x40,0x7e,0x00},
    ['F' - 32] = {0x7e,0x40,0x40,0x78,0x40,0x40,0x40,0x00},
    ['G' - 32] = {0x3c,0x42,0x40,0x4e,0x42,0x42,0x3c,0x00},
    ['H' - 32] = {0x42,0x42,0x42,0x7e,0x42,0x42,0x42,0x00},
    ['I' - 32] = {0x3e,0x08,0x08,0x08,0x08,0x08,0x3e,0x00},
    ['J' - 32] = {0x1e,0x04,0x04,0x04,0x44,0x44,0x38,0x00},
    ['K' - 32] = {0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00},
    ['L' - 32] = {0x40,0x40,0x40,0x40,0x40,0x40,0x7e,0x00},
    ['M' - 32] = {0x42,0x66,0x5a,0x5a,0x42,0x42,0x42,0x00},
    ['N' - 32] = {0x42,0x62,0x52,0x4a,0x46,0x42,0x42,0x00},
    ['O' - 32] = {0x3c,0x42,0x42,0x42,0x42,0x42,0x3c,0x00},
    ['P' - 32] = {0x7c,0x42,0x42,0x7c,0x40,0x40,0x40,0x00},
    ['Q' - 32] = {0x3c,0x42,0x42,0x42,0x4a,0x44,0x3a,0x00},
    ['R' - 32] = {0x7c,0x42,0x42,0x7c,0x48,0x44,0x42,0x00},
    ['S' - 32] = {0x3c,0x42,0x40,0x3c,0x02,0x42,0x3c,0x00},
    ['T' - 32] = {0x7e,0x08,0x08,0x08,0x08,0x08,0x08,0x00},
    ['U' - 32] = {0x42,0x42,0x42,0x42,0x42,0x42,0x3c,0x00},
    ['V' - 32] = {0x42,0x42,0x42,0x42,0x24,0x24,0x18,0x00},
    ['W' - 32] = {0x42,0x42,0x42,0x5a,0x5a,0x66,0x42,0x00},
    ['X' - 32] = {0x42,0x24,0x18,0x18,0x18,0x24,0x42,0x00},
    ['Y' - 32] = {0x42,0x24,0x18,0x08,0x08,0x08,0x08,0x00},
    ['Z' - 32] = {0x7e,0x02,0x04,0x18,0x20,0x40,0x7e,0x00},
    ['[' - 32] = {0x3c,0x20,0x20,0x20,0x20,0x20,0x3c,0x00},
    ['\\' - 32] = {0x40,0x20,0x10,0x08,0x04,0x02,0x00,0x00},
    [']' - 32] = {0x3c,0x04,0x04,0x04,0x04,0x04,0x3c,0x00},
    ['^' - 32] = {0x18,0x24,0x42,0x00,0x00,0x00,0x00,0x00},
    ['_' - 32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x7e,0x00},
    ['`' - 32] = {0x20,0x10,0x08,0x00,0x00,0x00,0x00,0x00},
    ['a' - 32] = {0x00,0x00,0x3c,0x02,0x3e,0x42,0x3e,0x00},
    ['b' - 32] = {0x40,0x40,0x5c,0x62,0x42,0x62,0x5c,0x00},
    ['c' - 32] = {0x00,0x00,0x3c,0x42,0x40,0x42,0x3c,0x00},
    ['d' - 32] = {0x02,0x02,0x3a,0x46,0x42,0x46,0x3a,0x00},
    ['e' - 32] = {0x00,0x00,0x3c,0x42,0x7e,0x40,0x3c,0x00},
    ['f' - 32] = {0x0c,0x12,0x10,0x3c,0x10,0x10,0x10,0x00},
    ['g' - 32] = {0x00,0x00,0x3a,0x46,0x46,0x3a,0x02,0x3c},
    ['h' - 32] = {0x40,0x40,0x5c,0x62,0x42,0x42,0x42,0x00},
    ['i' - 32] = {0x08,0x00,0x18,0x08,0x08,0x08,0x1c,0x00},
    ['j' - 32] = {0x04,0x00,0x0c,0x04,0x04,0x44,0x44,0x38},
    ['k' - 32] = {0x40,0x40,0x44,0x48,0x70,0x48,0x44,0x00},
    ['l' - 32] = {0x18,0x08,0x08,0x08,0x08,0x08,0x1c,0x00},
    ['m' - 32] = {0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x00},
    ['n' - 32] = {0x00,0x00,0x5c,0x62,0x42,0x42,0x42,0x00},
    ['o' - 32] = {0x00,0x00,0x3c,0x42,0x42,0x42,0x3c,0x00},
    ['p' - 32] = {0x00,0x00,0x5c,0x62,0x62,0x5c,0x40,0x40},
    ['q' - 32] = {0x00,0x00,0x3a,0x46,0x46,0x3a,0x02,0x02},
    ['r' - 32] = {0x00,0x00,0x5c,0x62,0x40,0x40,0x40,0x00},
    ['s' - 32] = {0x00,0x00,0x3e,0x40,0x3c,0x02,0x7c,0x00},
    ['t' - 32] = {0x10,0x10,0x7c,0x10,0x10,0x12,0x0c,0x00},
    ['u' - 32] = {0x00,0x00,0x42,0x42,0x42,0x46,0x3a,0x00},
    ['v' - 32] = {0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x00},
    ['w' - 32] = {0x00,0x00,0x41,0x49,0x49,0x49,0x36,0x00},
    ['x' - 32] = {0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00},
    ['y' - 32] = {0x00,0x00,0x42,0x42,0x46,0x3a,0x02,0x3c},
    ['z' - 32] = {0x00,0x00,0x7e,0x04,0x18,0x20,0x7e,0x00},
    ['{' - 32] = {0x0e,0x10,0x10,0x60,0x10,0x10,0x0e,0x00},
    ['|' - 32] = {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    ['}' - 32] = {0x70,0x08,0x08,0x06,0x08,0x08,0x70,0x00},
    ['~' - 32] = {0x00,0x00,0x32,0x4c,0x00,0x00,0x00,0x00},
};

static void dirty_reset(void) {
    g_dirty_x0 = 9999;
    g_dirty_y0 = 9999;
    g_dirty_x1 = -1;
    g_dirty_y1 = -1;
}

static void dirty_add_raw(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PRG32_LCD_W) w = PRG32_LCD_W - x;
    if (y + h > PRG32_LCD_H) h = PRG32_LCD_H - y;
    if (w <= 0 || h <= 0) return;
    if (x < g_dirty_x0) g_dirty_x0 = x;
    if (y < g_dirty_y0) g_dirty_y0 = y;
    if (x + w - 1 > g_dirty_x1) g_dirty_x1 = x + w - 1;
    if (y + h - 1 > g_dirty_y1) g_dirty_y1 = y + h - 1;
}

static int logical_h(void) {
    return g_fullscreen ? PRG32_LCD_H : PRG32_GAME_H;
}

static int logical_y_to_raw(int y) {
    return g_fullscreen ? y : y + PRG32_VIEWPORT_Y;
}

static void dirty_add(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PRG32_GAME_W) w = PRG32_GAME_W - x;
    if (y + h > logical_h()) h = logical_h() - y;
    if (w <= 0 || h <= 0) return;
    dirty_add_raw(x, logical_y_to_raw(y), w, h);
}

static const uint8_t *font_glyph(unsigned ch) {
    if (ch < 32 || ch > 126) {
        ch = '?';
    }
    return g_font8[ch - 32];
}

static void fill_raw_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > PRG32_LCD_W) {
        w = PRG32_LCD_W - x;
    }
    if (y + h > PRG32_LCD_H) {
        h = PRG32_LCD_H - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            g_fb[py * PRG32_LCD_W + px] = color;
        }
    }
    dirty_add_raw(x, y, w, h);
}

static void text8_raw(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    if (!s) {
        return;
    }
    int start_x = x;
    int chars = 0;
    while (*s && x < PRG32_LCD_W) {
        const uint8_t *glyph = font_glyph((unsigned char)*s++);
        for (int row = 0; row < 8; ++row) {
            int py = y + row;
            if ((unsigned)py >= PRG32_LCD_H) {
                continue;
            }
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                int px = x + col;
                if ((unsigned)px >= PRG32_LCD_W) {
                    continue;
                }
                g_fb[py * PRG32_LCD_W + px] =
                    (bits & (1u << (7 - col))) ? fg : bg;
            }
        }
        x += 8;
        chars++;
    }
    if (chars > 0) {
        dirty_add_raw(start_x, y, chars * 8, 8);
    }
}

static void draw_band_overlays(void) {
    if (g_fullscreen) {
        return;
    }
    uint32_t now = prg32_ticks_ms();
    for (uint8_t band = PRG32_BAND_TOP; band <= PRG32_BAND_BOTTOM; ++band) {
        int y = band == PRG32_BAND_TOP ? 0 : PRG32_VIEWPORT_Y + PRG32_GAME_H;
        if (!prg32_band_visible(band)) {
            if (g_band_cache_valid[band]) {
                fill_raw_rect(0, y, PRG32_LCD_W, PRG32_VIEWPORT_Y, g_last_band_color);
                g_band_cache_valid[band] = 0;
                g_band_text_cache[band][0] = '\0';
            }
            continue;
        }
        uint16_t bg = prg32_band_bg(band, g_last_band_color);
        uint16_t fg = prg32_band_fg(band);
        const char *text = prg32_band_render_text(band, now);
        if (g_band_cache_valid[band] &&
            g_band_fg_cache[band] == fg &&
            g_band_bg_cache[band] == bg &&
            strcmp(g_band_text_cache[band], text) == 0) {
            continue;
        }
        fill_raw_rect(0, y, PRG32_LCD_W, PRG32_VIEWPORT_Y, bg);
        text8_raw(4,
                  y + 6,
                  text,
                  fg,
                  bg);
        snprintf(g_band_text_cache[band],
                 sizeof(g_band_text_cache[band]),
                 "%s",
                 text);
        g_band_fg_cache[band] = fg;
        g_band_bg_cache[band] = bg;
        g_band_cache_valid[band] = 1;
    }
}

void prg32_display_init(void) {
    prg32_gfx_lock_init();
    esp_lcd_rgb_qemu_config_t cfg = {
        .width = PRG32_LCD_W,
        .height = PRG32_LCD_H,
        .bpp = RGB_QEMU_BPP_16,
    };

    esp_err_t err = esp_lcd_new_rgb_qemu(&cfg, &g_panel);
#if CONFIG_PRG32_QEMU_FAIL_ON_REAL_HW
    ESP_ERROR_CHECK(err);
#else
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "QEMU RGB panel unavailable: %s", esp_err_to_name(err));
        dirty_reset();
        return;
    }
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));
    ESP_LOGI(TAG, "QEMU RGB framebuffer ready at %dx%d", PRG32_LCD_W, PRG32_LCD_H);
    dirty_reset();
}

uint32_t prg32_ticks_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000u);
}

void prg32_gfx_clear(uint16_t color) {
    prg32_gfx_lock();
    if (g_fullscreen) {
        g_last_band_color = color;
        g_band_area_valid = 0;
        for (int i = 0; i < PRG32_LCD_W * PRG32_LCD_H; ++i) {
            g_fb[i] = color;
        }
        dirty_add_raw(0, 0, PRG32_LCD_W, PRG32_LCD_H);
        prg32_gfx_unlock();
        return;
    }

    uint16_t band = g_band_color_set ? g_band_color : color;
    if (!g_band_area_valid || band != g_last_band_color) {
        fill_raw_rect(0, 0, PRG32_LCD_W, PRG32_VIEWPORT_Y, band);
        fill_raw_rect(0,
                      PRG32_VIEWPORT_Y + PRG32_GAME_H,
                      PRG32_LCD_W,
                      PRG32_VIEWPORT_Y,
                      band);
        g_last_band_color = band;
        g_band_area_valid = 1;
        g_band_cache_valid[PRG32_BAND_TOP] = 0;
        g_band_cache_valid[PRG32_BAND_BOTTOM] = 0;
    }
    fill_raw_rect(0, PRG32_VIEWPORT_Y, PRG32_GAME_W, PRG32_GAME_H, color);
    prg32_gfx_unlock();
}

void prg32_gfx_pixel(int x, int y, uint16_t color) {
    prg32_gfx_lock();
    if ((unsigned)x >= PRG32_GAME_W || (unsigned)y >= (unsigned)logical_h()) {
        prg32_gfx_unlock();
        return;
    }
    int raw_y = logical_y_to_raw(y);
    g_fb[raw_y * PRG32_LCD_W + x] = color;
    dirty_add(x, y, 1, 1);
    prg32_gfx_unlock();
}

void prg32_gfx_rect(int x, int y, int w, int h, uint16_t color) {
    prg32_gfx_lock();
    if (w <= 0 || h <= 0) {
        prg32_gfx_unlock();
        return;
    }
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PRG32_GAME_W) x1 = PRG32_GAME_W;
    if (y1 > logical_h()) y1 = logical_h();
    if (x0 >= x1 || y0 >= y1) {
        prg32_gfx_unlock();
        return;
    }
    for (int py = y0; py < y1; ++py) {
        int raw_y = logical_y_to_raw(py);
        for (int px = x0; px < x1; ++px) {
            g_fb[raw_y * PRG32_LCD_W + px] = color;
        }
    }
    dirty_add(x0, y0, x1 - x0, y1 - y0);
    prg32_gfx_unlock();
}

void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    prg32_gfx_lock();
    if (!s) {
        prg32_gfx_unlock();
        return;
    }
    while (*s) {
        const uint8_t *glyph = font_glyph((unsigned char)*s++);
        for (int row = 0; row < 8; ++row) {
            uint8_t bits = glyph[row];
            int py = y + row;
            if ((unsigned)py >= (unsigned)logical_h()) {
                continue;
            }
            int raw_y = logical_y_to_raw(py);
            for (int col = 0; col < 8; ++col) {
                int px = x + col;
                if ((unsigned)px >= PRG32_GAME_W) {
                    continue;
                }
                g_fb[raw_y * PRG32_LCD_W + px] =
                    (bits & (1u << (7 - col))) ? fg : bg;
            }
        }
        dirty_add(x, y, 8, 8);
        x += 8;
    }
    prg32_gfx_unlock();
}

int prg32_gfx_snapshot_row_rgb565(int y, uint16_t *out, size_t pixels) {
    prg32_gfx_lock();
    if (!out || pixels < PRG32_LCD_W || (unsigned)y >= PRG32_LCD_H) {
        prg32_gfx_unlock();
        return -1;
    }
    memcpy(out, &g_fb[y * PRG32_LCD_W], PRG32_LCD_W * sizeof(g_fb[0]));
    prg32_gfx_unlock();
    return PRG32_LCD_W;
}

void prg32_gfx_present(void) {
    prg32_gfx_lock();
    if (!g_panel) {
        prg32_gfx_unlock();
        return;
    }
    draw_band_overlays();
    if (g_dirty_x1 < g_dirty_x0 || !g_panel) {
        prg32_band_note_frame(prg32_ticks_ms());
        prg32_gfx_unlock();
        return;
    }
    if (g_dirty_x0 == 0 && g_dirty_x1 == PRG32_LCD_W - 1) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            g_panel,
            0,
            g_dirty_y0,
            PRG32_LCD_W,
            g_dirty_y1 + 1,
            &g_fb[g_dirty_y0 * PRG32_LCD_W]));
    } else {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel,
                                                  0,
                                                  0,
                                                  PRG32_LCD_W,
                                                  PRG32_LCD_H,
                                                  g_fb));
    }
    dirty_reset();
    prg32_band_note_frame(prg32_ticks_ms());
    prg32_gfx_unlock();
}

void prg32_gfx_set_fullscreen(int enabled) {
    prg32_gfx_lock();
    g_fullscreen = enabled ? 1 : 0;
    if (g_fullscreen) {
        g_band_area_valid = 0;
        g_band_cache_valid[PRG32_BAND_TOP] = 0;
        g_band_cache_valid[PRG32_BAND_BOTTOM] = 0;
    }
    prg32_gfx_unlock();
}

int prg32_gfx_fullscreen_enabled(void) {
    prg32_gfx_lock();
    int enabled = g_fullscreen;
    prg32_gfx_unlock();
    return enabled;
}

void prg32_gfx_set_band_color(uint16_t color) {
    prg32_gfx_lock();
    g_band_color = color;
    g_last_band_color = color;
    g_band_color_set = 1;
    g_band_cache_valid[PRG32_BAND_TOP] = 0;
    g_band_cache_valid[PRG32_BAND_BOTTOM] = 0;
    if (!g_fullscreen) {
        for (int y = 0; y < PRG32_VIEWPORT_Y; ++y) {
            for (int x = 0; x < PRG32_LCD_W; ++x) {
                g_fb[y * PRG32_LCD_W + x] = color;
            }
        }
        for (int y = PRG32_VIEWPORT_Y + PRG32_GAME_H; y < PRG32_LCD_H; ++y) {
            for (int x = 0; x < PRG32_LCD_W; ++x) {
                g_fb[y * PRG32_LCD_W + x] = color;
            }
        }
        dirty_add_raw(0, 0, PRG32_LCD_W, PRG32_VIEWPORT_Y);
        dirty_add_raw(0,
                      PRG32_VIEWPORT_Y + PRG32_GAME_H,
                      PRG32_LCD_W,
                      PRG32_VIEWPORT_Y);
    }
    prg32_gfx_unlock();
}

void prg32_gfx_use_background_bands(void) {
    prg32_gfx_lock();
    g_band_color_set = 0;
    g_band_cache_valid[PRG32_BAND_TOP] = 0;
    g_band_cache_valid[PRG32_BAND_BOTTOM] = 0;
    prg32_gfx_unlock();
}
