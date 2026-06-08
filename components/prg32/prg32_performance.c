#include "prg32.h"
#include "prg32_config.h"

#include "cJSON.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_PRG32_METRICS_BOARD_ID
#define CONFIG_PRG32_METRICS_BOARD_ID "prg32-board"
#endif

#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "unknown"
#endif

#ifndef CONFIG_COMPILER_OPTIMIZATION_DEBUG
#define CONFIG_COMPILER_OPTIMIZATION_DEBUG 0
#endif

#define PRG32_PERF_SCREEN_FRAMES 60u
#define PRG32_PERF_SCREEN_COUNT 5u
#define PRG32_PERF_TEST_FRAMES (PRG32_PERF_SCREEN_FRAMES * PRG32_PERF_SCREEN_COUNT)
#define PRG32_PERF_SAMPLE_PERIOD_FRAMES 1u
#define PRG32_PERF_MAX_SAMPLES (PRG32_PERF_TEST_FRAMES + 16u)
#define PRG32_PERF_MAX_WINDOWS 80u
#define PRG32_PERF_WINDOW_US 1000000u
#define PRG32_PERF_FRAME_BUDGET_US 33333u
#define PRG32_PERF_SCREEN_NAME_LEN 24u
#define PRG32_PERF_SCREEN_GOAL_LEN 64u

#if CONFIG_PRG32_DISPLAY_QEMU_RGB
#define PRG32_PERF_TARGET_NAME "qemu-esp32c3"
#define PRG32_PERF_DISPLAY_BACKEND "qemu_rgb"
#else
#define PRG32_PERF_TARGET_NAME CONFIG_IDF_TARGET
#define PRG32_PERF_DISPLAY_BACKEND "ili9341"
#endif

#if CONFIG_COMPILER_OPTIMIZATION_DEBUG
#define PRG32_PERF_BUILD_TYPE "debug"
#else
#define PRG32_PERF_BUILD_TYPE "release"
#endif

typedef struct {
    uint32_t frame_index;
    uint64_t sampled_at_device_us;
    uint32_t t_update_us;
    uint32_t t_draw_us;
    uint32_t t_present_us;
    uint32_t t_frame_total_us;
    uint32_t free_heap_bytes;
    uint32_t min_free_heap_bytes;
    uint32_t input_mask;
    uint16_t upload_queue_depth;
    uint8_t screen_index;
    uint8_t deadline_missed;
} prg32_perf_sample_t;

typedef struct {
    uint32_t window_index;
    uint64_t started_at_device_us;
    uint32_t duration_us;
    uint32_t frames;
    uint32_t fps_mean_x100;
    uint32_t frame_us_min;
    uint32_t frame_us_mean;
    uint32_t frame_us_p50;
    uint32_t frame_us_p95;
    uint32_t frame_us_p99;
    uint32_t frame_us_max;
    uint32_t missed_deadlines;
    uint32_t update_us_mean;
    uint32_t draw_us_mean;
    uint32_t present_us_mean;
    uint32_t heap_min;
} prg32_perf_window_t;

typedef struct {
    uint8_t screen_index;
    char screen_name[PRG32_PERF_SCREEN_NAME_LEN];
    char metric_goal[PRG32_PERF_SCREEN_GOAL_LEN];
    uint32_t first_frame;
    uint32_t last_frame;
    prg32_perf_window_t metrics;
} prg32_perf_screen_result_t;

typedef struct {
    const char *name;
    const char *metric_goal;
    uint16_t background;
} prg32_perf_screen_def_t;

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int paddle_x;
    int scroll;
    uint32_t stars;
} prg32_perf_scene_t;

static prg32_perf_sample_t *g_samples;
static prg32_perf_window_t *g_windows;
static prg32_perf_screen_result_t g_screen_results[PRG32_PERF_SCREEN_COUNT];
static prg32_performance_summary_t g_summary;
static uint32_t g_sample_count;
static uint32_t g_window_count;
static uint32_t g_screen_result_count;
static uint32_t g_run_sequence;
static volatile int g_perf_running;
static volatile int g_perf_has_results;

static const prg32_perf_screen_def_t g_perf_screens[PRG32_PERF_SCREEN_COUNT] = {
    {
        "clear-fill",
        "viewport clear and large rectangle fill bandwidth",
        0x0008,
    },
    {
        "text-overlay",
        "8x8 text drawing and status overlay load",
        PRG32_COLOR_BLACK,
    },
    {
        "sprite-storm",
        "many moving sprite-sized rectangles and collision-like motion",
        0x0841,
    },
    {
        "scrolling",
        "horizontal and vertical scrolling with parallax-like stars",
        0x0008,
    },
    {
        "mixed-gameplay",
        "combined text, sprites, scrolling road, and playfield objects",
        0x0008,
    },
};

static int perf_buffers_alloc(void) {
    if (g_samples && g_windows) {
        return 0;
    }
    g_samples = calloc(PRG32_PERF_MAX_SAMPLES, sizeof(prg32_perf_sample_t));
    g_windows = calloc(PRG32_PERF_MAX_WINDOWS, sizeof(prg32_perf_window_t));
    if (!g_samples || !g_windows) {
        free(g_samples);
        free(g_windows);
        g_samples = NULL;
        g_windows = NULL;
        return -1;
    }
    return 0;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src && src[0] ? src : "");
}

static const char *wifi_mode_name(prg32_wifi_mode_t mode) {
    if (mode == PRG32_WIFI_MODE_STA) {
        return "infrastructure";
    }
    if (mode == PRG32_WIFI_MODE_AP) {
        return "access_point";
    }
    if (mode == PRG32_WIFI_MODE_APSTA) {
        return "ap_infrastructure";
    }
    return "off";
}

static uint32_t elapsed_u32(int64_t end_us, int64_t start_us) {
    int64_t delta = end_us - start_us;
    if (delta <= 0) {
        return 0;
    }
    if (delta > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)delta;
}

static void sort_u32(uint32_t *values, uint32_t count) {
    for (uint32_t i = 1; i < count; ++i) {
        uint32_t v = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > v) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = v;
    }
}

static uint32_t percentile_sorted(const uint32_t *values,
                                  uint32_t count,
                                  uint32_t percentile) {
    if (!values || count == 0) {
        return 0;
    }
    uint32_t rank = (count * percentile + 99u) / 100u;
    if (rank == 0) {
        rank = 1;
    }
    if (rank > count) {
        rank = count;
    }
    return values[rank - 1u];
}

static uint32_t mean_u64(uint64_t total, uint32_t count) {
    if (count == 0) {
        return 0;
    }
    return (uint32_t)(total / count);
}

static uint32_t fps_x100(uint32_t frames, uint32_t duration_us) {
    if (frames == 0 || duration_us == 0) {
        return 0;
    }
    uint64_t fps = (uint64_t)frames * 100000000ULL;
    return (uint32_t)(fps / duration_us);
}

static void draw_centered_line(int y, const char *text, uint16_t color) {
    int len = 0;
    if (text) {
        while (text[len]) {
            len++;
        }
    }
    int x = (PRG32_LCD_W - len * 8) / 2;
    if (x < 0) {
        x = 0;
    }
    prg32_gfx_text8(x, y, text ? text : "", color, 0);
}

static const char *screen_name(uint8_t screen_index) {
    if (screen_index >= PRG32_PERF_SCREEN_COUNT) {
        return "unknown";
    }
    return g_perf_screens[screen_index].name;
}

static const char *screen_goal(uint8_t screen_index) {
    if (screen_index >= PRG32_PERF_SCREEN_COUNT) {
        return "";
    }
    return g_perf_screens[screen_index].metric_goal;
}

static void draw_progress(uint32_t frame, uint8_t screen_index) {
    char line[48];
    uint32_t width = (frame * 240u) / PRG32_PERF_TEST_FRAMES;
    prg32_gfx_set_fullscreen(1);
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_centered_line(40, "PRG32 PERFORMANCE TEST", PRG32_COLOR_WHITE);
    draw_centered_line(64, "AUTOMATIC MULTI-SCREEN BENCHMARK", PRG32_COLOR_CYAN);
    prg32_gfx_rect(40, 112, 240, 14, PRG32_COLOR_BLUE);
    prg32_gfx_rect(42, 114, (int)width, 10, PRG32_COLOR_GREEN);
    snprintf(line,
             sizeof(line),
             "SCREEN %u/%u: %.20s",
             (unsigned)screen_index + 1u,
             (unsigned)PRG32_PERF_SCREEN_COUNT,
             screen_name(screen_index));
    draw_centered_line(88, line, PRG32_COLOR_YELLOW);
    snprintf(line,
             sizeof(line),
             "FRAME %lu / %lu",
             (unsigned long)frame,
             (unsigned long)PRG32_PERF_TEST_FRAMES);
    draw_centered_line(144, line, PRG32_COLOR_YELLOW);
    prg32_gfx_present();
}

static void scene_init(prg32_perf_scene_t *scene) {
    memset(scene, 0, sizeof(*scene));
    scene->x = 24;
    scene->y = 32;
    scene->vx = 3;
    scene->vy = 2;
    scene->paddle_x = 120;
    scene->stars = 0x13579bdfu;
}

static void scene_update(prg32_perf_scene_t *scene, uint32_t frame) {
    scene->scroll = (scene->scroll + 3) % PRG32_GAME_W;
    scene->paddle_x = 110 + (int)((frame * 5u) % 90u);
    scene->x += scene->vx;
    scene->y += scene->vy;
    if (scene->x <= 0 || scene->x >= PRG32_GAME_W - 12) {
        scene->vx = -scene->vx;
        scene->x += scene->vx;
    }
    if (scene->y <= 0 || scene->y >= PRG32_GAME_H - 12) {
        scene->vy = -scene->vy;
        scene->y += scene->vy;
    }
    scene->stars = scene->stars * 1664525u + 1013904223u;
}

static void draw_screen_header(uint8_t screen_index,
                               uint32_t global_frame,
                               uint32_t local_frame) {
    char line[40];
    prg32_gfx_text8(8, 8, screen_name(screen_index), PRG32_COLOR_WHITE, 0);
    snprintf(line,
             sizeof(line),
             "%03lu/%03lu",
             (unsigned long)local_frame,
             (unsigned long)PRG32_PERF_SCREEN_FRAMES);
    prg32_gfx_text8(240, 8, line, PRG32_COLOR_CYAN, 0);
    snprintf(line, sizeof(line), "F%03lu", (unsigned long)global_frame);
    prg32_gfx_text8(240, 20, line, PRG32_COLOR_YELLOW, 0);
}

static void scene_draw_clear_fill(uint8_t screen_index,
                                  const prg32_perf_scene_t *scene,
                                  uint32_t global_frame,
                                  uint32_t local_frame) {
    (void)scene;
    uint16_t bg = (local_frame & 1u) ? 0x0008 : 0x0100;
    prg32_gfx_clear(bg);
    draw_screen_header(screen_index, global_frame, local_frame);
    for (int i = 0; i < 8; ++i) {
        int y = 38 + i * 18;
        int w = 300 - i * 22;
        uint16_t color = (i & 1) ? PRG32_COLOR_BLUE : PRG32_COLOR_MAGENTA;
        prg32_gfx_rect(10 + i * 7, y, w, 10, color);
        prg32_gfx_rect(12 + i * 7, y + 2, w - 4, 2, PRG32_COLOR_CYAN);
    }
}

static void scene_draw_text_overlay(uint8_t screen_index,
                                    const prg32_perf_scene_t *scene,
                                    uint32_t global_frame,
                                    uint32_t local_frame) {
    static const char *rows[] = {
        "REGISTER TRACE  A0 A1 A2 A3",
        "MEMORY VIEW     0000 1000 2000",
        "STACK           RA SP S0 S1",
        "FRAME BUDGET    33333 US",
        "WIFI            AP/STA METADATA",
        "CARTRIDGE       ABI PRG2",
        "AUDIO           MIXER CHECK",
        "INPUT           LOCAL MASK",
    };
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    draw_screen_header(screen_index, global_frame, local_frame);
    for (int y = 32; y < PRG32_GAME_H; y += 8) {
        int row = (y / 8 + (int)local_frame) & 7;
        uint16_t color = (row & 1) ? PRG32_COLOR_GREEN : PRG32_COLOR_CYAN;
        prg32_gfx_text8(8, y, rows[row], color, 0);
        if ((row & 3) == 0) {
            char value[32];
            snprintf(value,
                     sizeof(value),
                     "%04lx %04lx",
                     (unsigned long)(global_frame * 17u + (uint32_t)y),
                     (unsigned long)(scene->stars >> (row & 7)));
            prg32_gfx_text8(216, y, value, PRG32_COLOR_YELLOW, 0);
        }
    }
}

static void scene_draw_sprite_storm(uint8_t screen_index,
                                    const prg32_perf_scene_t *scene,
                                    uint32_t global_frame,
                                    uint32_t local_frame) {
    static const uint16_t colors[] = {
        PRG32_COLOR_RED,
        PRG32_COLOR_GREEN,
        PRG32_COLOR_BLUE,
        PRG32_COLOR_YELLOW,
        PRG32_COLOR_CYAN,
        PRG32_COLOR_MAGENTA,
    };
    prg32_gfx_clear(g_perf_screens[screen_index].background);
    draw_screen_header(screen_index, global_frame, local_frame);
    for (int i = 0; i < 36; ++i) {
        int x = (int)((i * 23 + local_frame * (2 + (i & 3))) % (PRG32_GAME_W - 14));
        int y = 30 + (int)((i * 19 + (scene->stars >> (i & 7))) % 150u);
        uint16_t color = colors[(i + (int)local_frame) % 6];
        prg32_gfx_rect(x, y, 12, 12, color);
        prg32_gfx_rect(x + 3, y + 3, 6, 6, PRG32_COLOR_WHITE);
        prg32_gfx_pixel(x + (i & 7), y + ((i * 3) & 7), PRG32_COLOR_BLACK);
    }
    prg32_gfx_rect(scene->x, scene->y, 18, 18, PRG32_COLOR_WHITE);
    prg32_gfx_rect(scene->x + 4, scene->y + 4, 10, 10, PRG32_COLOR_RED);
}

static void scene_draw_scrolling(uint8_t screen_index,
                                 const prg32_perf_scene_t *scene,
                                 uint32_t global_frame,
                                 uint32_t local_frame) {
    static const uint16_t star_colors[] = {
        PRG32_COLOR_WHITE,
        PRG32_COLOR_CYAN,
        PRG32_COLOR_YELLOW,
        PRG32_COLOR_MAGENTA,
    };
    prg32_gfx_clear(g_perf_screens[screen_index].background);
    draw_screen_header(screen_index, global_frame, local_frame);

    for (int i = 0; i < 72; ++i) {
        int speed = 1 + (i & 3);
        int x = (int)((i * 37 + scene->scroll * speed) % PRG32_GAME_W);
        int y = 28 + (int)((i * 17 + local_frame * (uint32_t)(i & 1)) % 130u);
        prg32_gfx_pixel(x, y, star_colors[i & 3]);
        if ((i & 7) == 0) {
            prg32_gfx_pixel((x + 1) % PRG32_GAME_W, y, star_colors[i & 3]);
        }
    }

    for (int y = 150; y < PRG32_GAME_H; y += 8) {
        int lane = (scene->scroll * 2 + y * 3) % 44;
        prg32_gfx_rect(0, y, PRG32_GAME_W, 1, 0x4208);
        for (int x = -lane; x < PRG32_GAME_W; x += 44) {
            prg32_gfx_rect(x, y + 3, 20, 2, PRG32_COLOR_YELLOW);
        }
    }
}

static void scene_draw_mixed_gameplay(uint8_t screen_index,
                                      const prg32_perf_scene_t *scene,
                                      uint32_t global_frame,
                                      uint32_t local_frame) {
    static const uint16_t star_colors[] = {
        PRG32_COLOR_WHITE,
        PRG32_COLOR_CYAN,
        PRG32_COLOR_YELLOW,
        PRG32_COLOR_MAGENTA,
    };
    prg32_gfx_clear(g_perf_screens[screen_index].background);
    draw_screen_header(screen_index, global_frame, local_frame);

    for (int i = 0; i < 42; ++i) {
        int x = (int)((i * 37 + scene->scroll * (1 + (i & 3))) % PRG32_GAME_W);
        int y = 24 + (int)((i * 29 + (scene->stars >> (i & 7))) % 128u);
        prg32_gfx_pixel(x, y, star_colors[i & 3]);
    }

    for (int y = 152; y < PRG32_GAME_H; y += 10) {
        int lane = (scene->scroll + y * 3) % 48;
        prg32_gfx_rect(0, y, PRG32_GAME_W, 1, 0x4208);
        for (int x = -lane; x < PRG32_GAME_W; x += 48) {
            prg32_gfx_rect(x, y + 4, 24, 2, PRG32_COLOR_YELLOW);
        }
    }

    for (int i = 0; i < 8; ++i) {
        int x = 24 + i * 34;
        int h = 18 + (int)((global_frame + i * 9u) % 28u);
        uint16_t color = (i & 1) ? PRG32_COLOR_BLUE : PRG32_COLOR_MAGENTA;
        prg32_gfx_rect(x, 132 - h, 22, h, color);
        prg32_gfx_rect(x + 4, 136 - h, 4, 4, PRG32_COLOR_CYAN);
    }

    prg32_gfx_rect(scene->paddle_x, 184, 58, 6, PRG32_COLOR_GREEN);
    prg32_gfx_rect(scene->x, scene->y, 12, 12, PRG32_COLOR_RED);
    prg32_gfx_rect(scene->x + 3, scene->y + 3, 6, 6, PRG32_COLOR_WHITE);
}

static void scene_draw_screen(uint8_t screen_index,
                              const prg32_perf_scene_t *scene,
                              uint32_t global_frame,
                              uint32_t local_frame) {
    switch (screen_index) {
    case 0:
        scene_draw_clear_fill(screen_index, scene, global_frame, local_frame);
        break;
    case 1:
        scene_draw_text_overlay(screen_index, scene, global_frame, local_frame);
        break;
    case 2:
        scene_draw_sprite_storm(screen_index, scene, global_frame, local_frame);
        break;
    case 3:
        scene_draw_scrolling(screen_index, scene, global_frame, local_frame);
        break;
    default:
        scene_draw_mixed_gameplay(screen_index, scene, global_frame, local_frame);
        break;
    }
}

static void store_sample(const prg32_perf_sample_t *sample) {
    if (!sample || !g_samples || g_sample_count >= PRG32_PERF_MAX_SAMPLES) {
        return;
    }
    g_samples[g_sample_count++] = *sample;
}

static void compute_range_stats(prg32_perf_window_t *w,
                                uint32_t window_index,
                                uint32_t first,
                                uint32_t last) {
    if (!w || first >= last) {
        return;
    }

    uint64_t update_total = 0;
    uint64_t draw_total = 0;
    uint64_t present_total = 0;
    uint64_t frame_total = 0;
    uint32_t frame_values[PRG32_PERF_MAX_SAMPLES];
    uint32_t heap_min = UINT32_MAX;
    uint32_t missed = 0;
    uint32_t count = last - first;
    uint64_t started_us = g_samples[first].sampled_at_device_us;
    uint64_t ended_us = g_samples[last - 1u].sampled_at_device_us;

    for (uint32_t i = first; i < last; ++i) {
        const prg32_perf_sample_t *s = &g_samples[i];
        update_total += s->t_update_us;
        draw_total += s->t_draw_us;
        present_total += s->t_present_us;
        frame_total += s->t_frame_total_us;
        frame_values[i - first] = s->t_frame_total_us;
        if (s->min_free_heap_bytes < heap_min) {
            heap_min = s->min_free_heap_bytes;
        }
        if (s->deadline_missed) {
            missed++;
        }
    }
    sort_u32(frame_values, count);

    memset(w, 0, sizeof(*w));
    w->window_index = window_index;
    w->started_at_device_us = started_us;
    w->duration_us = (uint32_t)(ended_us > started_us ? ended_us - started_us : 0);
    w->frames = count;
    w->frame_us_min = frame_values[0];
    w->frame_us_mean = mean_u64(frame_total, count);
    w->fps_mean_x100 = w->frame_us_mean
        ? (uint32_t)(100000000ULL / w->frame_us_mean)
        : fps_x100(count, w->duration_us);
    w->frame_us_p50 = percentile_sorted(frame_values, count, 50);
    w->frame_us_p95 = percentile_sorted(frame_values, count, 95);
    w->frame_us_p99 = percentile_sorted(frame_values, count, 99);
    w->frame_us_max = frame_values[count - 1u];
    w->missed_deadlines = missed;
    w->update_us_mean = mean_u64(update_total, count);
    w->draw_us_mean = mean_u64(draw_total, count);
    w->present_us_mean = mean_u64(present_total, count);
    w->heap_min = heap_min == UINT32_MAX ? 0 : heap_min;
}

static void compute_window(uint32_t window_index, uint32_t first, uint32_t last) {
    if (!g_windows || g_window_count >= PRG32_PERF_MAX_WINDOWS || first >= last) {
        return;
    }
    compute_range_stats(&g_windows[g_window_count++], window_index, first, last);
}

static void compute_screen_results(void) {
    g_screen_result_count = 0;
    if (!g_samples) {
        return;
    }
    for (uint8_t screen_index = 0; screen_index < PRG32_PERF_SCREEN_COUNT; ++screen_index) {
        uint32_t first = UINT32_MAX;
        uint32_t last = 0;
        for (uint32_t i = 0; i < g_sample_count; ++i) {
            if (g_samples[i].screen_index != screen_index) {
                continue;
            }
            if (first == UINT32_MAX) {
                first = i;
            }
            last = i + 1u;
        }
        if (first == UINT32_MAX || last <= first ||
            g_screen_result_count >= PRG32_PERF_SCREEN_COUNT) {
            continue;
        }
        prg32_perf_screen_result_t *result = &g_screen_results[g_screen_result_count++];
        memset(result, 0, sizeof(*result));
        result->screen_index = screen_index;
        copy_text(result->screen_name, sizeof(result->screen_name), screen_name(screen_index));
        copy_text(result->metric_goal, sizeof(result->metric_goal), screen_goal(screen_index));
        result->first_frame = g_samples[first].frame_index;
        result->last_frame = g_samples[last - 1u].frame_index;
        compute_range_stats(&result->metrics, screen_index, first, last);
    }
}

static void compute_results(uint64_t started_us, uint64_t finished_us) {
    g_window_count = 0;
    g_screen_result_count = 0;
    memset(&g_summary, 0, sizeof(g_summary));
    if (!g_samples || !g_windows || g_sample_count == 0) {
        return;
    }

    uint32_t first = 0;
    uint32_t window_index = 0;
    uint64_t window_end = started_us + PRG32_PERF_WINDOW_US;
    for (uint32_t i = 0; i < g_sample_count; ++i) {
        if (g_samples[i].sampled_at_device_us >= window_end) {
            compute_window(window_index++, first, i);
            first = i;
            while (g_samples[i].sampled_at_device_us >= window_end) {
                window_end += PRG32_PERF_WINDOW_US;
            }
        }
    }
    compute_window(window_index, first, g_sample_count);
    compute_screen_results();

    prg32_perf_window_t summary_window;
    compute_range_stats(&summary_window, UINT32_MAX, 0, g_sample_count);

    snprintf(g_summary.run_id,
             sizeof(g_summary.run_id),
             "perf-%llu-%lu",
             (unsigned long long)(started_us / 1000ULL),
             (unsigned long)g_run_sequence++);
    copy_text(g_summary.board_id, sizeof(g_summary.board_id), CONFIG_PRG32_METRICS_BOARD_ID);
    copy_text(g_summary.target, sizeof(g_summary.target), PRG32_PERF_TARGET_NAME);
    copy_text(g_summary.display_backend,
              sizeof(g_summary.display_backend),
              PRG32_PERF_DISPLAY_BACKEND);
    copy_text(g_summary.firmware_version,
              sizeof(g_summary.firmware_version),
              PRG32_FIRMWARE_VERSION);
    copy_text(g_summary.firmware_git_sha, sizeof(g_summary.firmware_git_sha), "unknown");
    copy_text(g_summary.build_type, sizeof(g_summary.build_type), PRG32_PERF_BUILD_TYPE);
    copy_text(g_summary.wifi_mode,
              sizeof(g_summary.wifi_mode),
              wifi_mode_name(prg32_wifi_current_mode()));
    copy_text(g_summary.game_name, sizeof(g_summary.game_name), "setup-performance-test");
    g_summary.cartridge_generation = prg32_cart_generation();
    g_summary.sample_period_frames = PRG32_PERF_SAMPLE_PERIOD_FRAMES;
    g_summary.started_at_device_us = started_us;
    g_summary.duration_us = (uint32_t)(finished_us > started_us
                                           ? finished_us - started_us
                                           : 0);
    g_summary.frames = g_sample_count;
    g_summary.sample_count = g_sample_count;
    g_summary.window_count = g_window_count;
    g_summary.screen_count = g_screen_result_count;
    g_summary.fps_mean_x100 = summary_window.fps_mean_x100;
    g_summary.frame_us_min = summary_window.frame_us_min;
    g_summary.frame_us_mean = summary_window.frame_us_mean;
    g_summary.frame_us_p50 = summary_window.frame_us_p50;
    g_summary.frame_us_p95 = summary_window.frame_us_p95;
    g_summary.frame_us_p99 = summary_window.frame_us_p99;
    g_summary.frame_us_max = summary_window.frame_us_max;
    g_summary.missed_deadlines = summary_window.missed_deadlines;
    g_summary.update_us_mean = summary_window.update_us_mean;
    g_summary.draw_us_mean = summary_window.draw_us_mean;
    g_summary.present_us_mean = summary_window.present_us_mean;
    g_summary.heap_min = summary_window.heap_min;
}

static void draw_summary(void) {
    char line[72];
    prg32_gfx_set_fullscreen(1);
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "PERFORMANCE TEST SUMMARY", PRG32_COLOR_WHITE, 0);
    snprintf(line,
             sizeof(line),
             "RUN: %.40s",
             g_summary.run_id);
    prg32_gfx_text8(8, 28, line, PRG32_COLOR_CYAN, 0);
    snprintf(line,
             sizeof(line),
             "OVERALL FPS: %lu.%02lu  SCREENS: %lu",
             (unsigned long)(g_summary.fps_mean_x100 / 100u),
             (unsigned long)(g_summary.fps_mean_x100 % 100u),
             (unsigned long)g_summary.screen_count);
    prg32_gfx_text8(8, 48, line, PRG32_COLOR_GREEN, 0);
    snprintf(line,
             sizeof(line),
             "FRAME US MEAN/P95/P99: %lu / %lu / %lu",
             (unsigned long)g_summary.frame_us_mean,
             (unsigned long)g_summary.frame_us_p95,
             (unsigned long)g_summary.frame_us_p99);
    prg32_gfx_text8(8, 64, line, PRG32_COLOR_YELLOW, 0);
    snprintf(line,
             sizeof(line),
             "UPDATE/DRAW/PRESENT: %lu / %lu / %lu",
             (unsigned long)g_summary.update_us_mean,
             (unsigned long)g_summary.draw_us_mean,
             (unsigned long)g_summary.present_us_mean);
    prg32_gfx_text8(8, 80, line, PRG32_COLOR_WHITE, 0);
    snprintf(line,
             sizeof(line),
             "MISSED: %lu/%lu  HEAP MIN: %lu",
             (unsigned long)g_summary.missed_deadlines,
             (unsigned long)g_summary.frames,
             (unsigned long)g_summary.heap_min);
    prg32_gfx_text8(8, 96, line, PRG32_COLOR_MAGENTA, 0);
    prg32_gfx_text8(8, 120, "SCREEN              FPS   P95US  MISS", PRG32_COLOR_CYAN, 0);
    for (uint32_t i = 0; i < g_screen_result_count && i < 5u; ++i) {
        const prg32_perf_screen_result_t *screen = &g_screen_results[i];
        snprintf(line,
                 sizeof(line),
                 "%-18.18s %2lu.%02lu %5lu %4lu",
                 screen->screen_name,
                 (unsigned long)(screen->metrics.fps_mean_x100 / 100u),
                 (unsigned long)(screen->metrics.fps_mean_x100 % 100u),
                 (unsigned long)screen->metrics.frame_us_p95,
                 (unsigned long)screen->metrics.missed_deadlines);
        prg32_gfx_text8(8, 136 + (int)i * 14, line, PRG32_COLOR_WHITE, 0);
    }
    prg32_gfx_text8(8,
                     212,
                     "DOWNLOAD: /api/performance.json",
                     PRG32_COLOR_GREEN,
                     0);
    prg32_gfx_text8(8,
                     224,
                     "A / SELECT / B BACK",
                     PRG32_COLOR_CYAN,
                     0);
    prg32_gfx_present();
}

static void wait_for_summary_exit(void) {
    uint32_t last = prg32_input_read_menu();
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if (((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) ||
            ((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_SELECT | PRG32_BTN_B);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(80));
        last = input;
    }
}

int prg32_performance_test_run(void) {
    if (perf_buffers_alloc() != 0) {
        memset(&g_summary, 0, sizeof(g_summary));
        g_perf_running = 0;
        g_perf_has_results = 0;
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "PERFORMANCE TEST", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 48, "NOT ENOUGH RAM", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_present();
        wait_for_summary_exit();
        return -1;
    }
    prg32_perf_scene_t scene;
    scene_init(&scene);
    g_perf_running = 1;
    g_perf_has_results = 0;
    g_sample_count = 0;
    g_window_count = 0;
    g_screen_result_count = 0;
    memset(g_samples, 0, PRG32_PERF_MAX_SAMPLES * sizeof(g_samples[0]));
    memset(g_windows, 0, PRG32_PERF_MAX_WINDOWS * sizeof(g_windows[0]));
    memset(g_screen_results, 0, sizeof(g_screen_results));
    memset(&g_summary, 0, sizeof(g_summary));

    draw_progress(0, 0);
    vTaskDelay(pdMS_TO_TICKS(300));

    prg32_gfx_set_fullscreen(0);
    prg32_gfx_use_background_bands();
    uint64_t started_us = (uint64_t)esp_timer_get_time();
    uint32_t frame = 0;
    for (uint8_t screen_index = 0; screen_index < PRG32_PERF_SCREEN_COUNT; ++screen_index) {
        draw_progress(frame, screen_index);
        vTaskDelay(pdMS_TO_TICKS(120));
        prg32_gfx_set_fullscreen(0);
        prg32_gfx_use_background_bands();

        for (uint32_t local_frame = 0;
             local_frame < PRG32_PERF_SCREEN_FRAMES;
             ++local_frame, ++frame) {
            uint32_t input_mask = prg32_input_read_menu();
            int64_t frame_start = esp_timer_get_time();
            prg32_gfx_lock();
            int64_t update_start = esp_timer_get_time();
            scene_update(&scene, frame);
            int64_t update_end = esp_timer_get_time();
            int64_t draw_start = update_end;
            scene_draw_screen(screen_index, &scene, frame, local_frame);
            int64_t draw_end = esp_timer_get_time();
            int64_t present_start = draw_end;
            prg32_gfx_present();
            int64_t present_end = esp_timer_get_time();
            prg32_gfx_unlock();

            prg32_perf_sample_t sample = {
                .frame_index = frame,
                .sampled_at_device_us = (uint64_t)present_end,
                .t_update_us = elapsed_u32(update_end, update_start),
                .t_draw_us = elapsed_u32(draw_end, draw_start),
                .t_present_us = elapsed_u32(present_end, present_start),
                .t_frame_total_us = elapsed_u32(present_end, frame_start),
                .free_heap_bytes = esp_get_free_heap_size(),
                .min_free_heap_bytes = esp_get_minimum_free_heap_size(),
                .input_mask = input_mask,
                .upload_queue_depth = 0,
                .screen_index = screen_index,
                .deadline_missed =
                    elapsed_u32(present_end, frame_start) > PRG32_PERF_FRAME_BUDGET_US,
            };
            store_sample(&sample);

            if ((frame & 15u) == 15u) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
    }

    uint64_t finished_us = (uint64_t)esp_timer_get_time();
    compute_results(started_us, finished_us);
    g_perf_has_results = 1;
    g_perf_running = 0;
    draw_summary();
    wait_for_summary_exit();
    prg32_gfx_set_fullscreen(1);
    return 0;
}

int prg32_performance_has_results(void) {
    return g_perf_has_results && !g_perf_running;
}

int prg32_performance_summary(prg32_performance_summary_t *out) {
    if (!out || !prg32_performance_has_results()) {
        return -1;
    }
    *out = g_summary;
    return 0;
}

static int stream_write(prg32_performance_json_writer_t writer,
                        void *ctx,
                        const char *chunk) {
    if (!writer || !chunk) {
        return -1;
    }
    return writer(chunk, ctx);
}

static int stream_writef(prg32_performance_json_writer_t writer,
                         void *ctx,
                         const char *fmt,
                         ...) {
    char chunk[768];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(chunk, sizeof(chunk), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(chunk)) {
        return -1;
    }
    return stream_write(writer, ctx, chunk);
}

static int stream_json_string(prg32_performance_json_writer_t writer,
                              void *ctx,
                              const char *value) {
    if (stream_write(writer, ctx, "\"") != 0) {
        return -1;
    }
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    while (*p) {
        char escaped[8];
        if (*p == '"' || *p == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)*p;
            escaped[2] = '\0';
            if (stream_write(writer, ctx, escaped) != 0) {
                return -1;
            }
        } else if (*p == '\n') {
            if (stream_write(writer, ctx, "\\n") != 0) {
                return -1;
            }
        } else if (*p == '\r') {
            if (stream_write(writer, ctx, "\\r") != 0) {
                return -1;
            }
        } else if (*p == '\t') {
            if (stream_write(writer, ctx, "\\t") != 0) {
                return -1;
            }
        } else if (*p < 0x20) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)*p);
            if (stream_write(writer, ctx, escaped) != 0) {
                return -1;
            }
        } else {
            escaped[0] = (char)*p;
            escaped[1] = '\0';
            if (stream_write(writer, ctx, escaped) != 0) {
                return -1;
            }
        }
        p++;
    }
    return stream_write(writer, ctx, "\"");
}

static int stream_json_string_field(prg32_performance_json_writer_t writer,
                                    void *ctx,
                                    const char *name,
                                    const char *value,
                                    int leading_comma) {
    if (stream_writef(writer, ctx, "%s\"%s\":", leading_comma ? "," : "", name) != 0) {
        return -1;
    }
    return stream_json_string(writer, ctx, value);
}

static int stream_summary_fields(prg32_performance_json_writer_t writer,
                                 void *ctx) {
    return stream_writef(writer,
                         ctx,
                         "\"frames\":%lu,"
                         "\"fps_mean\":%lu.%02lu,"
                         "\"frame_us_min\":%lu,"
                         "\"frame_us_mean\":%lu,"
                         "\"frame_us_p50\":%lu,"
                         "\"frame_us_p95\":%lu,"
                         "\"frame_us_p99\":%lu,"
                         "\"frame_us_max\":%lu,"
                         "\"missed_deadlines\":%lu,"
                         "\"update_us_mean\":%lu,"
                         "\"draw_us_mean\":%lu,"
                         "\"present_us_mean\":%lu,"
                         "\"heap_min\":%lu,"
                         "\"screen_count\":%lu",
                         (unsigned long)g_summary.frames,
                         (unsigned long)(g_summary.fps_mean_x100 / 100u),
                         (unsigned long)(g_summary.fps_mean_x100 % 100u),
                         (unsigned long)g_summary.frame_us_min,
                         (unsigned long)g_summary.frame_us_mean,
                         (unsigned long)g_summary.frame_us_p50,
                         (unsigned long)g_summary.frame_us_p95,
                         (unsigned long)g_summary.frame_us_p99,
                         (unsigned long)g_summary.frame_us_max,
                         (unsigned long)g_summary.missed_deadlines,
                         (unsigned long)g_summary.update_us_mean,
                         (unsigned long)g_summary.draw_us_mean,
                         (unsigned long)g_summary.present_us_mean,
                         (unsigned long)g_summary.heap_min,
                         (unsigned long)g_summary.screen_count);
}

int prg32_performance_json_write(prg32_performance_json_writer_t writer,
                                 void *ctx) {
    if (!writer) {
        return -1;
    }
    if (g_perf_running) {
        return stream_write(writer, ctx, "{\"ok\":false,\"running\":true}");
    }
    if (!g_perf_has_results) {
        return stream_write(writer,
                            ctx,
                            "{\"ok\":false,\"error\":\"no performance test results\"}");
    }

    if (stream_write(writer, ctx, "{\"ok\":true") != 0 ||
        stream_json_string_field(writer, ctx, "run_id", g_summary.run_id, 1) != 0 ||
        stream_json_string_field(writer, ctx, "board_id", g_summary.board_id, 1) != 0 ||
        stream_json_string_field(writer, ctx, "target", g_summary.target, 1) != 0 ||
        stream_json_string_field(writer,
                                 ctx,
                                 "display_backend",
                                 g_summary.display_backend,
                                 1) != 0 ||
        stream_json_string_field(writer,
                                 ctx,
                                 "firmware_git_sha",
                                 g_summary.firmware_git_sha,
                                 1) != 0 ||
        stream_json_string_field(writer,
                                 ctx,
                                 "firmware_version",
                                 g_summary.firmware_version,
                                 1) != 0 ||
        stream_json_string_field(writer, ctx, "game_name", g_summary.game_name, 1) != 0) {
        return -1;
    }
    if (stream_writef(writer,
                      ctx,
                      ",\"cartridge_generation\":%lu",
                      (unsigned long)g_summary.cartridge_generation) != 0 ||
        stream_json_string_field(writer, ctx, "build_type", g_summary.build_type, 1) != 0 ||
        stream_json_string_field(writer, ctx, "wifi_mode", g_summary.wifi_mode, 1) != 0 ||
        stream_writef(writer,
                      ctx,
                      ",\"sample_period_frames\":%lu,"
                      "\"screen_count\":%lu,"
                      "\"started_at_device_us\":%llu,"
                      "\"started_at_server_ts\":null,"
                      "\"duration_us\":%lu,"
                      "\"samples\":[",
                      (unsigned long)g_summary.sample_period_frames,
                      (unsigned long)g_summary.screen_count,
                      (unsigned long long)g_summary.started_at_device_us,
                      (unsigned long)g_summary.duration_us) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < g_sample_count; ++i) {
        const prg32_perf_sample_t *s = &g_samples[i];
        if (stream_writef(writer,
                          ctx,
                          "%s{"
                          "\"frame_index\":%lu,"
                          "\"screen_index\":%lu,"
                          "\"screen_name\":\"%s\","
                          "\"sampled_at_device_us\":%llu,"
                          "\"t_update_us\":%lu,"
                          "\"t_draw_us\":%lu,"
                          "\"t_present_us\":%lu,"
                          "\"t_frame_total_us\":%lu,"
                          "\"deadline_missed\":%s,"
                          "\"free_heap_bytes\":%lu,"
                          "\"min_free_heap_bytes\":%lu,"
                          "\"input_mask\":%lu,"
                          "\"upload_queue_depth\":%lu"
                          "}",
                          i ? "," : "",
                          (unsigned long)s->frame_index,
                          (unsigned long)s->screen_index,
                          screen_name(s->screen_index),
                          (unsigned long long)s->sampled_at_device_us,
                          (unsigned long)s->t_update_us,
                          (unsigned long)s->t_draw_us,
                          (unsigned long)s->t_present_us,
                          (unsigned long)s->t_frame_total_us,
                          s->deadline_missed ? "true" : "false",
                          (unsigned long)s->free_heap_bytes,
                          (unsigned long)s->min_free_heap_bytes,
                          (unsigned long)s->input_mask,
                          (unsigned long)s->upload_queue_depth) != 0) {
            return -1;
        }
    }

    if (stream_write(writer, ctx, "],\"aggregate_windows\":[") != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < g_window_count; ++i) {
        const prg32_perf_window_t *w = &g_windows[i];
        if (stream_writef(writer,
                          ctx,
                          "%s{"
                          "\"window_index\":%lu,"
                          "\"started_at_device_us\":%llu,"
                          "\"duration_us\":%lu,"
                          "\"frames\":%lu,"
                          "\"fps_mean\":%lu.%02lu,"
                          "\"frame_us_min\":%lu,"
                          "\"frame_us_mean\":%lu,"
                          "\"frame_us_p50\":%lu,"
                          "\"frame_us_p95\":%lu,"
                          "\"frame_us_p99\":%lu,"
                          "\"frame_us_max\":%lu,"
                          "\"missed_deadlines\":%lu,"
                          "\"update_us_mean\":%lu,"
                          "\"draw_us_mean\":%lu,"
                          "\"present_us_mean\":%lu,"
                          "\"heap_min\":%lu"
                          "}",
                          i ? "," : "",
                          (unsigned long)w->window_index,
                          (unsigned long long)w->started_at_device_us,
                          (unsigned long)w->duration_us,
                          (unsigned long)w->frames,
                          (unsigned long)(w->fps_mean_x100 / 100u),
                          (unsigned long)(w->fps_mean_x100 % 100u),
                          (unsigned long)w->frame_us_min,
                          (unsigned long)w->frame_us_mean,
                          (unsigned long)w->frame_us_p50,
                          (unsigned long)w->frame_us_p95,
                          (unsigned long)w->frame_us_p99,
                          (unsigned long)w->frame_us_max,
                          (unsigned long)w->missed_deadlines,
                          (unsigned long)w->update_us_mean,
                          (unsigned long)w->draw_us_mean,
                          (unsigned long)w->present_us_mean,
                          (unsigned long)w->heap_min) != 0) {
            return -1;
        }
    }

    if (stream_write(writer, ctx, "],\"screen_summaries\":[") != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < g_screen_result_count; ++i) {
        const prg32_perf_screen_result_t *screen = &g_screen_results[i];
        const prg32_perf_window_t *w = &screen->metrics;
        if (stream_writef(writer,
                          ctx,
                          "%s{"
                          "\"screen_index\":%lu,"
                          "\"screen_name\":\"%s\","
                          "\"metric_goal\":\"%s\","
                          "\"first_frame\":%lu,"
                          "\"last_frame\":%lu,"
                          "\"frames\":%lu,"
                          "\"fps_mean\":%lu.%02lu,"
                          "\"frame_us_min\":%lu,"
                          "\"frame_us_mean\":%lu,"
                          "\"frame_us_p50\":%lu,"
                          "\"frame_us_p95\":%lu,"
                          "\"frame_us_p99\":%lu,"
                          "\"frame_us_max\":%lu,"
                          "\"missed_deadlines\":%lu,"
                          "\"update_us_mean\":%lu,"
                          "\"draw_us_mean\":%lu,"
                          "\"present_us_mean\":%lu,"
                          "\"heap_min\":%lu"
                          "}",
                          i ? "," : "",
                          (unsigned long)screen->screen_index,
                          screen->screen_name,
                          screen->metric_goal,
                          (unsigned long)screen->first_frame,
                          (unsigned long)screen->last_frame,
                          (unsigned long)w->frames,
                          (unsigned long)(w->fps_mean_x100 / 100u),
                          (unsigned long)(w->fps_mean_x100 % 100u),
                          (unsigned long)w->frame_us_min,
                          (unsigned long)w->frame_us_mean,
                          (unsigned long)w->frame_us_p50,
                          (unsigned long)w->frame_us_p95,
                          (unsigned long)w->frame_us_p99,
                          (unsigned long)w->frame_us_max,
                          (unsigned long)w->missed_deadlines,
                          (unsigned long)w->update_us_mean,
                          (unsigned long)w->draw_us_mean,
                          (unsigned long)w->present_us_mean,
                          (unsigned long)w->heap_min) != 0) {
            return -1;
        }
    }

    if (stream_write(writer, ctx, "],\"summary\":{") != 0 ||
        stream_summary_fields(writer, ctx) != 0 ||
        stream_write(writer, ctx, "}}") != 0) {
        return -1;
    }
    return 0;
}

static void json_u32(cJSON *obj, const char *name, uint32_t value) {
    cJSON_AddNumberToObject(obj, name, (double)value);
}

static void json_u64(cJSON *obj, const char *name, uint64_t value) {
    cJSON_AddNumberToObject(obj, name, (double)value);
}

static void json_fps(cJSON *obj, const char *name, uint32_t fps_value_x100) {
    cJSON_AddNumberToObject(obj, name, (double)fps_value_x100 / 100.0);
}

static void add_summary_json(cJSON *obj) {
    json_u32(obj, "frames", g_summary.frames);
    json_fps(obj, "fps_mean", g_summary.fps_mean_x100);
    json_u32(obj, "frame_us_min", g_summary.frame_us_min);
    json_u32(obj, "frame_us_mean", g_summary.frame_us_mean);
    json_u32(obj, "frame_us_p50", g_summary.frame_us_p50);
    json_u32(obj, "frame_us_p95", g_summary.frame_us_p95);
    json_u32(obj, "frame_us_p99", g_summary.frame_us_p99);
    json_u32(obj, "frame_us_max", g_summary.frame_us_max);
    json_u32(obj, "missed_deadlines", g_summary.missed_deadlines);
    json_u32(obj, "update_us_mean", g_summary.update_us_mean);
    json_u32(obj, "draw_us_mean", g_summary.draw_us_mean);
    json_u32(obj, "present_us_mean", g_summary.present_us_mean);
    json_u32(obj, "heap_min", g_summary.heap_min);
    json_u32(obj, "screen_count", g_summary.screen_count);
}

static void add_window_metrics_json(cJSON *obj, const prg32_perf_window_t *w) {
    if (!obj || !w) {
        return;
    }
    json_u32(obj, "frames", w->frames);
    json_fps(obj, "fps_mean", w->fps_mean_x100);
    json_u32(obj, "frame_us_min", w->frame_us_min);
    json_u32(obj, "frame_us_mean", w->frame_us_mean);
    json_u32(obj, "frame_us_p50", w->frame_us_p50);
    json_u32(obj, "frame_us_p95", w->frame_us_p95);
    json_u32(obj, "frame_us_p99", w->frame_us_p99);
    json_u32(obj, "frame_us_max", w->frame_us_max);
    json_u32(obj, "missed_deadlines", w->missed_deadlines);
    json_u32(obj, "update_us_mean", w->update_us_mean);
    json_u32(obj, "draw_us_mean", w->draw_us_mean);
    json_u32(obj, "present_us_mean", w->present_us_mean);
    json_u32(obj, "heap_min", w->heap_min);
}

char *prg32_performance_json_alloc(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    if (g_perf_running) {
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddBoolToObject(root, "running", true);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        return json;
    }
    if (!g_perf_has_results) {
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "error", "no performance test results");
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        return json;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "run_id", g_summary.run_id);
    cJSON_AddStringToObject(root, "board_id", g_summary.board_id);
    cJSON_AddStringToObject(root, "target", g_summary.target);
    cJSON_AddStringToObject(root, "display_backend", g_summary.display_backend);
    cJSON_AddStringToObject(root, "firmware_git_sha", g_summary.firmware_git_sha);
    cJSON_AddStringToObject(root, "firmware_version", g_summary.firmware_version);
    cJSON_AddStringToObject(root, "game_name", g_summary.game_name);
    json_u32(root, "cartridge_generation", g_summary.cartridge_generation);
    cJSON_AddStringToObject(root, "build_type", g_summary.build_type);
    cJSON_AddStringToObject(root, "wifi_mode", g_summary.wifi_mode);
    json_u32(root, "sample_period_frames", g_summary.sample_period_frames);
    json_u32(root, "screen_count", g_summary.screen_count);
    json_u64(root, "started_at_device_us", g_summary.started_at_device_us);
    cJSON_AddNullToObject(root, "started_at_server_ts");
    json_u32(root, "duration_us", g_summary.duration_us);

    cJSON *samples = cJSON_AddArrayToObject(root, "samples");
    for (uint32_t i = 0; samples && i < g_sample_count; ++i) {
        const prg32_perf_sample_t *s = &g_samples[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        json_u32(item, "frame_index", s->frame_index);
        json_u32(item, "screen_index", s->screen_index);
        cJSON_AddStringToObject(item, "screen_name", screen_name(s->screen_index));
        json_u64(item, "sampled_at_device_us", s->sampled_at_device_us);
        json_u32(item, "t_update_us", s->t_update_us);
        json_u32(item, "t_draw_us", s->t_draw_us);
        json_u32(item, "t_present_us", s->t_present_us);
        json_u32(item, "t_frame_total_us", s->t_frame_total_us);
        cJSON_AddBoolToObject(item, "deadline_missed", s->deadline_missed != 0);
        json_u32(item, "free_heap_bytes", s->free_heap_bytes);
        json_u32(item, "min_free_heap_bytes", s->min_free_heap_bytes);
        json_u32(item, "input_mask", s->input_mask);
        json_u32(item, "upload_queue_depth", s->upload_queue_depth);
        cJSON_AddItemToArray(samples, item);
    }

    cJSON *windows = cJSON_AddArrayToObject(root, "aggregate_windows");
    for (uint32_t i = 0; windows && i < g_window_count; ++i) {
        const prg32_perf_window_t *w = &g_windows[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        json_u32(item, "window_index", w->window_index);
        json_u64(item, "started_at_device_us", w->started_at_device_us);
        json_u32(item, "duration_us", w->duration_us);
        add_window_metrics_json(item, w);
        cJSON_AddItemToArray(windows, item);
    }

    cJSON *screens = cJSON_AddArrayToObject(root, "screen_summaries");
    for (uint32_t i = 0; screens && i < g_screen_result_count; ++i) {
        const prg32_perf_screen_result_t *screen = &g_screen_results[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        json_u32(item, "screen_index", screen->screen_index);
        cJSON_AddStringToObject(item, "screen_name", screen->screen_name);
        cJSON_AddStringToObject(item, "metric_goal", screen->metric_goal);
        json_u32(item, "first_frame", screen->first_frame);
        json_u32(item, "last_frame", screen->last_frame);
        add_window_metrics_json(item, &screen->metrics);
        cJSON_AddItemToArray(screens, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    if (summary) {
        add_summary_json(summary);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

void prg32_performance_json_free(char *json) {
    if (json) {
        cJSON_free(json);
    }
}
