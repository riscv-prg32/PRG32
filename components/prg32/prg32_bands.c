#include "prg32.h"
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

#define BAND_TEXT_LEN 56
#define BAND_NVS_NAMESPACE "prg32band"

typedef struct {
    prg32_band_mode_t mode;
    uint16_t fg;
    uint16_t bg;
    uint8_t custom_bg;
    char text[BAND_TEXT_LEN];
    char render[BAND_TEXT_LEN + 16];
} prg32_band_state_t;

static prg32_band_state_t g_band[2] = {
    {
        .mode = PRG32_BAND_MODE_NONE,
        .fg = PRG32_COLOR_CYAN,
        .bg = PRG32_COLOR_BLACK,
        .custom_bg = 0,
    },
    {
        .mode = PRG32_BAND_MODE_NONE,
        .fg = PRG32_COLOR_GREEN,
        .bg = PRG32_COLOR_BLACK,
        .custom_bg = 0,
    },
};

static char g_game_info[BAND_TEXT_LEN];
static char g_debug_info[BAND_TEXT_LEN];
static uint32_t g_fps_window_ms;
static uint32_t g_fps_frames;
static uint32_t g_fps_value;

static int valid_band(uint8_t band) {
    return band == PRG32_BAND_TOP || band == PRG32_BAND_BOTTOM;
}

static prg32_band_mode_t normalize_mode(prg32_band_mode_t mode) {
    if (mode < PRG32_BAND_MODE_NONE || mode > PRG32_BAND_MODE_CUSTOM) {
        return PRG32_BAND_MODE_NONE;
    }
    return mode;
}

static void copy_printable(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t out = 0;
    while (*src && out + 1 < dst_size) {
        unsigned char ch = (unsigned char)*src++;
        dst[out++] = (ch >= 32 && ch <= 126) ? (char)ch : '?';
    }
    dst[out] = '\0';
}

static esp_err_t band_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK;
        }
    }
    return err;
}

void prg32_band_set_mode(uint8_t band, prg32_band_mode_t mode) {
    if (!valid_band(band)) {
        return;
    }
    g_band[band].mode = normalize_mode(mode);
}

prg32_band_mode_t prg32_band_mode(uint8_t band) {
    if (!valid_band(band)) {
        return PRG32_BAND_MODE_NONE;
    }
    return g_band[band].mode;
}

const char *prg32_band_mode_name(prg32_band_mode_t mode) {
    switch (normalize_mode(mode)) {
        case PRG32_BAND_MODE_FPS: return "FPS";
        case PRG32_BAND_MODE_WIFI: return "WIFI";
        case PRG32_BAND_MODE_GAME: return "GAME";
        case PRG32_BAND_MODE_DEBUG: return "DEBUG";
        case PRG32_BAND_MODE_CUSTOM: return "CUSTOM";
        case PRG32_BAND_MODE_NONE:
        default:
            return "OFF";
    }
}

void prg32_band_set_text(uint8_t band, const char *text) {
    if (!valid_band(band)) {
        return;
    }
    copy_printable(g_band[band].text, sizeof(g_band[band].text), text);
}

void prg32_band_set_game_info(const char *text) {
    copy_printable(g_game_info, sizeof(g_game_info), text);
}

void prg32_band_log(const char *message) {
    copy_printable(g_debug_info, sizeof(g_debug_info), message);
}

void prg32_band_set_colors(uint8_t band, uint16_t fg, uint16_t bg) {
    if (!valid_band(band)) {
        return;
    }
    g_band[band].fg = fg;
    g_band[band].bg = bg;
    g_band[band].custom_bg = 1;
}

void prg32_band_use_default_colors(uint8_t band) {
    if (!valid_band(band)) {
        return;
    }
    g_band[band].custom_bg = 0;
}

void prg32_band_load_config(void) {
    if (band_nvs_init() != ESP_OK) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(BAND_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    uint8_t top = (uint8_t)g_band[PRG32_BAND_TOP].mode;
    uint8_t bottom = (uint8_t)g_band[PRG32_BAND_BOTTOM].mode;
    nvs_get_u8(nvs, "top", &top);
    nvs_get_u8(nvs, "bottom", &bottom);
    g_band[PRG32_BAND_TOP].mode = normalize_mode((prg32_band_mode_t)top);
    g_band[PRG32_BAND_BOTTOM].mode = normalize_mode((prg32_band_mode_t)bottom);
    nvs_close(nvs);
}

void prg32_band_save_config(void) {
    if (band_nvs_init() != ESP_OK) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(BAND_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_u8(nvs, "top", (uint8_t)g_band[PRG32_BAND_TOP].mode);
    nvs_set_u8(nvs, "bottom", (uint8_t)g_band[PRG32_BAND_BOTTOM].mode);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void prg32_band_note_frame(uint32_t now_ms) {
    if (g_fps_window_ms == 0) {
        g_fps_window_ms = now_ms;
        g_fps_frames = 0;
        g_fps_value = 0;
        return;
    }
    g_fps_frames++;
    uint32_t elapsed = now_ms - g_fps_window_ms;
    if (elapsed >= 1000u) {
        g_fps_value = (g_fps_frames * 1000u) / elapsed;
        g_fps_frames = 0;
        g_fps_window_ms = now_ms;
    }
}

int prg32_band_visible(uint8_t band) {
    return valid_band(band) && g_band[band].mode != PRG32_BAND_MODE_NONE;
}

uint16_t prg32_band_fg(uint8_t band) {
    if (!valid_band(band)) {
        return PRG32_COLOR_WHITE;
    }
    return g_band[band].fg;
}

uint16_t prg32_band_bg(uint8_t band, uint16_t fallback) {
    if (!valid_band(band) || !g_band[band].custom_bg) {
        return fallback;
    }
    return g_band[band].bg;
}

const char *prg32_band_render_text(uint8_t band, uint32_t now_ms) {
    if (!valid_band(band)) {
        return "";
    }
    prg32_band_state_t *state = &g_band[band];
    state->render[0] = '\0';
    switch (state->mode) {
        case PRG32_BAND_MODE_FPS:
            snprintf(state->render, sizeof(state->render), "FPS:%lu",
                     (unsigned long)g_fps_value);
            break;
        case PRG32_BAND_MODE_WIFI:
        {
            const char *ssid = prg32_wifi_current_ssid();
            const char *ip = prg32_wifi_current_ip();
            snprintf(state->render,
                     sizeof(state->render),
                     "WIFI:%s IP:%s",
                     ssid ? ssid : "",
                     ip ? ip : "0.0.0.0");
            break;
        }
        case PRG32_BAND_MODE_GAME:
            if (g_game_info[0]) {
                snprintf(state->render, sizeof(state->render), "%s", g_game_info);
            } else {
                prg32_cart_info_t info;
                if (prg32_cart_get_info(&info) == 0 && info.loaded) {
                    snprintf(state->render,
                             sizeof(state->render),
                             "%s %.32s",
                             info.slot_name,
                             info.name[0] ? info.name : "(unnamed)");
                } else {
                    snprintf(state->render, sizeof(state->render), "PRG32");
                }
            }
            break;
        case PRG32_BAND_MODE_DEBUG:
            snprintf(state->render,
                     sizeof(state->render),
                     "%s",
                     g_debug_info[0] ? g_debug_info : "DEBUG");
            break;
        case PRG32_BAND_MODE_CUSTOM:
            copy_printable(state->render, sizeof(state->render), state->text);
            break;
        case PRG32_BAND_MODE_NONE:
        default:
            break;
    }
    (void)now_ms;
    return state->render;
}
