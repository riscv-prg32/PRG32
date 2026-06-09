#include "prg32_metrics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifndef CONFIG_PRG32_METRICS_ENABLE
#define CONFIG_PRG32_METRICS_ENABLE 0
#endif

#if CONFIG_PRG32_METRICS_ENABLE

#include "cJSON.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#ifndef CONFIG_PRG32_METRICS_QUEUE_LEN
#define CONFIG_PRG32_METRICS_QUEUE_LEN 128
#endif

#ifndef CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES
#define CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES 1
#endif

#ifndef CONFIG_PRG32_METRICS_UPLOAD_PERIOD_MS
#define CONFIG_PRG32_METRICS_UPLOAD_PERIOD_MS 5000
#endif

#ifndef CONFIG_PRG32_METRICS_SERVER_URL
#define CONFIG_PRG32_METRICS_SERVER_URL "http://192.168.4.2:8080"
#endif

#ifndef CONFIG_PRG32_METRICS_BOARD_ID
#define CONFIG_PRG32_METRICS_BOARD_ID "prg32-board"
#endif

#define PRG32_METRICS_BATCH_MAX 64
#define PRG32_METRICS_HTTP_TIMEOUT_MS 1200
#define PRG32_METRICS_TASK_STACK 8192
#define PRG32_METRICS_TASK_PRIORITY 3

#if CONFIG_PRG32_METRICS_QUEUE_LEN < 8
#define PRG32_METRICS_QUEUE_CAP 8
#else
#define PRG32_METRICS_QUEUE_CAP CONFIG_PRG32_METRICS_QUEUE_LEN
#endif

static const char *TAG = "prg32_metrics";

typedef struct {
    int enabled;
    uint32_t sample_period_frames;
    uint32_t upload_period_ms;
    char server_url[128];
    char board_id[40];
    char target[24];
    char display_backend[24];
    char firmware_version[32];
    char firmware_git_sha[24];
    char game_name[40];
} prg32_metrics_state_config_t;

static prg32_metrics_state_config_t g_config = {
    .enabled = CONFIG_PRG32_METRICS_ENABLE,
    .sample_period_frames = CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES,
    .upload_period_ms = CONFIG_PRG32_METRICS_UPLOAD_PERIOD_MS,
    .server_url = CONFIG_PRG32_METRICS_SERVER_URL,
    .board_id = CONFIG_PRG32_METRICS_BOARD_ID,
    .target = "unknown",
    .display_backend = "unknown",
    .firmware_version = "dev",
    .firmware_git_sha = "unknown",
    .game_name = "idle",
};

static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
static prg32_metric_sample_t *g_queue;
static uint16_t g_head;
static uint16_t g_tail;
static uint16_t g_count;
static uint32_t g_dropped_pending;
static uint32_t g_dropped_total;
static uint32_t g_sequence;
static int64_t g_run_started_us;
static int64_t g_run_finished_us;
static bool g_initialized;
static bool g_running;
static bool g_run_registered;
static bool g_finish_pending;
static TaskHandle_t g_task;
static char g_run_id[96];

static void copy_field(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src || !src[0]) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static uint32_t safe_u32(int64_t value) {
    if (value <= 0) {
        return 0;
    }
    if (value > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)value;
}

static uint32_t config_period_frames(void) {
    return g_config.sample_period_frames ? g_config.sample_period_frames : 1;
}

static uint32_t config_upload_ms(void) {
    return g_config.upload_period_ms ? g_config.upload_period_ms : 5000;
}

static void queue_clear_locked(void) {
    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_dropped_pending = 0;
}

static bool queue_alloc(void) {
    if (g_queue) {
        return true;
    }
    g_queue = heap_caps_calloc(PRG32_METRICS_QUEUE_CAP,
                               sizeof(g_queue[0]),
                               MALLOC_CAP_8BIT);
    if (!g_queue) {
        ESP_LOGW(TAG,
                 "could not allocate metrics queue (%u samples)",
                 (unsigned)PRG32_METRICS_QUEUE_CAP);
        return false;
    }
    return true;
}

static void queue_free(void) {
    prg32_metric_sample_t *queue = NULL;
    taskENTER_CRITICAL(&g_lock);
    queue = g_queue;
    g_queue = NULL;
    queue_clear_locked();
    taskEXIT_CRITICAL(&g_lock);
    heap_caps_free(queue);
}

static bool queue_empty(void) {
    taskENTER_CRITICAL(&g_lock);
    bool empty = g_count == 0 && g_dropped_pending == 0;
    taskEXIT_CRITICAL(&g_lock);
    return empty;
}

static void make_run_id(void) {
    int64_t now_us = esp_timer_get_time();
    snprintf(g_run_id,
             sizeof(g_run_id),
             "%s-%lld-%lu",
             g_config.board_id[0] ? g_config.board_id : "prg32",
             (long long)(now_us / 1000),
             (unsigned long)g_sequence++);
}

static void append_json_u32(cJSON *obj, const char *name, uint32_t value) {
    cJSON_AddNumberToObject(obj, name, (double)value);
}

static bool build_url(char *out, size_t out_size, const char *path) {
    if (!out || out_size == 0 || !path || !g_config.server_url[0]) {
        return false;
    }
    const size_t base_len = strlen(g_config.server_url);
    const int has_slash = base_len > 0 && g_config.server_url[base_len - 1] == '/';
    const int path_slash = path[0] == '/';
    int written = snprintf(out,
                           out_size,
                           "%s%s%s",
                           g_config.server_url,
                           has_slash || !path_slash ? "" : "/",
                           has_slash && path_slash ? path + 1 : path);
    return written > 0 && (size_t)written < out_size;
}

static bool http_post_json(const char *path, cJSON *root) {
    char url[192];
    if (!build_url(url, sizeof(url), path)) {
        return false;
    }

    char *body = cJSON_PrintUnformatted(root);
    if (!body) {
        return false;
    }

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = PRG32_METRICS_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        cJSON_free(body);
        return false;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(body);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "POST %s failed: err=%s status=%d", path, esp_err_to_name(err), status);
        return false;
    }
    return true;
}

static bool post_run_start(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddStringToObject(root, "run_id", g_run_id);
    cJSON_AddStringToObject(root, "board_id", g_config.board_id);
    cJSON_AddStringToObject(root, "target", g_config.target);
    cJSON_AddStringToObject(root, "display_backend", g_config.display_backend);
    cJSON_AddStringToObject(root, "firmware_version", g_config.firmware_version);
    cJSON_AddStringToObject(root, "firmware_git_sha", g_config.firmware_git_sha);
    cJSON_AddStringToObject(root, "game_name", g_config.game_name);
    append_json_u32(root, "sample_period_frames", config_period_frames());
    append_json_u32(root, "started_ms", safe_u32(g_run_started_us / 1000));

    bool ok = http_post_json("/api/runs", root);
    cJSON_Delete(root);
    return ok;
}

static bool post_run_finish(void) {
    char path[128];
    snprintf(path, sizeof(path), "/api/runs/%s/finish", g_run_id);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    append_json_u32(root, "finished_ms", safe_u32(g_run_finished_us / 1000));
    append_json_u32(root, "dropped_samples", g_dropped_total);
    bool ok = http_post_json(path, root);
    cJSON_Delete(root);
    return ok;
}

static bool post_batch(const prg32_metric_sample_t *samples,
                       size_t count,
                       uint32_t dropped_samples) {
    if (!samples || (count == 0 && dropped_samples == 0)) {
        return true;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *array = root ? cJSON_AddArrayToObject(root, "samples") : NULL;
    if (!root || !array) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_AddStringToObject(root, "run_id", g_run_id);
    append_json_u32(root, "dropped_samples", dropped_samples);

    for (size_t i = 0; i < count; ++i) {
        const prg32_metric_sample_t *s = &samples[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            return false;
        }
        append_json_u32(item, "frame", s->frame);
        append_json_u32(item, "timestamp_ms", s->timestamp_ms);
        append_json_u32(item, "update_us", s->update_us);
        append_json_u32(item, "draw_us", s->draw_us);
        append_json_u32(item, "present_us", s->present_us);
        append_json_u32(item, "frame_us", s->frame_us);
        append_json_u32(item, "heap_free", s->heap_free);
        append_json_u32(item, "heap_min_free", s->heap_min_free);
        append_json_u32(item, "input_mask", s->input_mask);
        append_json_u32(item, "fps_x100", s->fps_x100);
        append_json_u32(item, "upload_queue_depth", s->upload_queue_depth);
        cJSON_AddBoolToObject(item, "deadline_missed", s->deadline_missed != 0);
        cJSON_AddItemToArray(array, item);
    }

    bool ok = http_post_json("/api/metrics/batch", root);
    cJSON_Delete(root);
    return ok;
}

static size_t pop_batch(prg32_metric_sample_t *out,
                        size_t max_count,
                        uint32_t *dropped_samples) {
    if (!out || max_count == 0) {
        return 0;
    }

    taskENTER_CRITICAL(&g_lock);
    size_t count = g_count < max_count ? g_count : max_count;
    for (size_t i = 0; i < count; ++i) {
        out[i] = g_queue[g_tail];
        g_tail = (uint16_t)((g_tail + 1u) % PRG32_METRICS_QUEUE_CAP);
    }
    g_count = (uint16_t)(g_count - count);
    if (dropped_samples) {
        *dropped_samples = g_dropped_pending;
    }
    g_dropped_pending = 0;
    taskEXIT_CRITICAL(&g_lock);
    return count;
}

static void restore_dropped(uint32_t already_counted, uint32_t newly_lost) {
    taskENTER_CRITICAL(&g_lock);
    g_dropped_pending += already_counted + newly_lost;
    g_dropped_total += newly_lost;
    taskEXIT_CRITICAL(&g_lock);
}

static void metrics_upload_task(void *arg) {
    (void)arg;
    prg32_metric_sample_t batch[PRG32_METRICS_BATCH_MAX];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(config_upload_ms()));

        if (!g_config.enabled) {
            continue;
        }

        if (g_running && !g_run_registered) {
            g_run_registered = post_run_start();
        }

        if (g_run_registered) {
            uint32_t dropped = 0;
            size_t count = pop_batch(batch, PRG32_METRICS_BATCH_MAX, &dropped);
            if (count > 0) {
                if (!post_batch(batch, count, dropped)) {
                    restore_dropped(dropped, (uint32_t)count);
                }
            } else if (dropped > 0) {
                if (!post_batch(batch, 0, dropped)) {
                    restore_dropped(dropped, 0);
                }
            }
        }

        if (!g_running && g_finish_pending && g_run_registered && queue_empty()) {
            if (post_run_finish()) {
                g_finish_pending = false;
                g_run_registered = false;
                queue_free();
            }
        } else if (!g_running && g_finish_pending && !g_run_registered) {
            g_finish_pending = false;
            queue_free();
        }
    }
}

int prg32_metrics_init(const prg32_metrics_config_t *config) {
    if (config) {
        g_config.enabled = config->enabled;
        copy_field(g_config.server_url, sizeof(g_config.server_url), config->server_url);
        copy_field(g_config.board_id, sizeof(g_config.board_id), config->board_id);
        copy_field(g_config.target, sizeof(g_config.target), config->target);
        copy_field(g_config.display_backend,
                   sizeof(g_config.display_backend),
                   config->display_backend);
        copy_field(g_config.firmware_version,
                   sizeof(g_config.firmware_version),
                   config->firmware_version);
        copy_field(g_config.firmware_git_sha,
                   sizeof(g_config.firmware_git_sha),
                   config->firmware_git_sha);
        copy_field(g_config.game_name, sizeof(g_config.game_name), config->game_name);
        g_config.sample_period_frames =
            config->sample_period_frames ? config->sample_period_frames : 1;
        g_config.upload_period_ms =
            config->upload_period_ms ? config->upload_period_ms : 5000;
    }

    if (!g_config.server_url[0]) {
        copy_field(g_config.server_url, sizeof(g_config.server_url), CONFIG_PRG32_METRICS_SERVER_URL);
    }
    if (!g_config.board_id[0]) {
        copy_field(g_config.board_id, sizeof(g_config.board_id), CONFIG_PRG32_METRICS_BOARD_ID);
    }

    g_initialized = true;
    if (g_config.enabled && !g_task) {
        BaseType_t ok = xTaskCreate(metrics_upload_task,
                                    "prg32_metrics",
                                    PRG32_METRICS_TASK_STACK,
                                    NULL,
                                    PRG32_METRICS_TASK_PRIORITY,
                                    &g_task);
        if (ok != pdPASS) {
            ESP_LOGW(TAG, "could not start metrics upload task");
            g_config.enabled = 0;
            return -1;
        }
    }
    return 0;
}

int prg32_metrics_start_run(void) {
    if (!g_initialized) {
        prg32_metrics_init(NULL);
    }
    if (!g_config.enabled) {
        return 0;
    }
    if (!queue_alloc()) {
        g_config.enabled = 0;
        return -1;
    }

    taskENTER_CRITICAL(&g_lock);
    queue_clear_locked();
    g_dropped_total = 0;
    taskEXIT_CRITICAL(&g_lock);

    g_run_started_us = esp_timer_get_time();
    g_run_finished_us = 0;
    g_running = true;
    g_run_registered = false;
    g_finish_pending = false;
    make_run_id();
    ESP_LOGI(TAG, "metrics run started: %s", g_run_id);
    return 0;
}

int prg32_metrics_stop_run(void) {
    if (!g_config.enabled || !g_running) {
        return 0;
    }
    g_running = false;
    g_finish_pending = true;
    g_run_finished_us = esp_timer_get_time();
    return 0;
}

int prg32_metrics_is_enabled(void) {
    return g_config.enabled && g_running;
}

int prg32_metrics_record(const prg32_metric_sample_t *sample) {
    if (!sample || !g_config.enabled || !g_running || !g_queue) {
        return 0;
    }

    if (config_period_frames() > 1 && sample->frame % config_period_frames() != 0) {
        return 0;
    }

    taskENTER_CRITICAL(&g_lock);
    if (g_count >= PRG32_METRICS_QUEUE_CAP) {
        g_dropped_pending++;
        g_dropped_total++;
        taskEXIT_CRITICAL(&g_lock);
        return -1;
    }

    prg32_metric_sample_t copy = *sample;
    copy.upload_queue_depth = (uint16_t)(g_count + 1u);
    g_queue[g_head] = copy;
    g_head = (uint16_t)((g_head + 1u) % PRG32_METRICS_QUEUE_CAP);
    g_count++;
    taskEXIT_CRITICAL(&g_lock);
    return 0;
}

const char *prg32_metrics_run_id(void) {
    return g_run_id;
}

#else

int prg32_metrics_init(const prg32_metrics_config_t *config) {
    (void)config;
    return 0;
}

int prg32_metrics_start_run(void) {
    return 0;
}

int prg32_metrics_stop_run(void) {
    return 0;
}

int prg32_metrics_is_enabled(void) {
    return 0;
}

int prg32_metrics_record(const prg32_metric_sample_t *sample) {
    (void)sample;
    return 0;
}

const char *prg32_metrics_run_id(void) {
    return "";
}

#endif
