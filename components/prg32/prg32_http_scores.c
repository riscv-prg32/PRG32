#include "prg32.h"
#include "prg32_config.h"

#include <stdio.h>
#include <string.h>

#if PRG32_WIFI_ENABLE
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#endif
#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("esp_err.h")
#include "esp_err.h"
#else
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#endif

#ifndef CONFIG_PRG32_STORE_URL
#define CONFIG_PRG32_STORE_URL ""
#endif

#if __has_include("freertos/FreeRTOS.h")
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(int ticks) {
    (void)ticks;
}
#endif

typedef struct {
    prg32_score_t score;
    uint8_t pending_remote;
} score_entry_t;

#define SCORE_NVS_PARTITION "scores"
#define SCORE_NVS_NAMESPACE "prg32score"
#define SCORE_NVS_MAGIC 0x50533332u
#define SCORE_NVS_VERSION 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    char current_player[sizeof(((prg32_score_t *)0)->player)];
    score_entry_t entries[PRG32_SCORE_MAX];
} score_store_t;

static score_entry_t scores[PRG32_SCORE_MAX];
static int score_count;
static char current_player[sizeof(scores[0].score.player)] = "PLAYER";
static int scores_loaded;
static int scores_persistence_available = 1;
static int scores_nvs_ready;

#if PRG32_WIFI_ENABLE
static int resolve_configured_store_url(char *out_url, size_t max_len);
#endif

static void copy_cstr(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int score_matches_game(const prg32_score_t *record, const char *game) {
    return record && (!game || !game[0] || strcmp(record->game, game) == 0);
}

static int score_entry_matches_game(const score_entry_t *entry, const char *game) {
    return entry && score_matches_game(&entry->score, game);
}

static int score_visible_index(const char *game, int visible_index) {
    int seen = 0;
    for (int i = 0; i < score_count; ++i) {
        if (!score_entry_matches_game(&scores[i], game)) {
            continue;
        }
        if (seen == visible_index) {
            return i;
        }
        seen++;
    }
    return -1;
}

static esp_err_t scores_nvs_init(void) {
    if (scores_nvs_ready) {
        return ESP_OK;
    }
    if (!scores_persistence_available) {
        return ESP_FAIL;
    }
    esp_err_t err = nvs_flash_init_partition(SCORE_NVS_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(SCORE_NVS_PARTITION));
        err = nvs_flash_init_partition(SCORE_NVS_PARTITION);
    }
    if (err != ESP_OK) {
        scores_persistence_available = 0;
    } else {
        scores_nvs_ready = 1;
    }
    return err;
}

static void scores_load_once(void) {
    if (scores_loaded) {
        return;
    }
    scores_loaded = 1;
    if (scores_nvs_init() != ESP_OK) {
        return;
    }

    nvs_handle_t nvs;
    if (nvs_open_from_partition(SCORE_NVS_PARTITION,
                                SCORE_NVS_NAMESPACE,
                                NVS_READONLY,
                                &nvs) != ESP_OK) {
        return;
    }
    score_store_t store;
    size_t size = sizeof(store);
    esp_err_t err = nvs_get_blob(nvs, "scores", &store, &size);
    nvs_close(nvs);
    if (err != ESP_OK ||
        size != sizeof(store) ||
        store.magic != SCORE_NVS_MAGIC ||
        store.version != SCORE_NVS_VERSION ||
        store.count > PRG32_SCORE_MAX) {
        return;
    }

    score_count = (int)store.count;
    memcpy(scores, store.entries, sizeof(scores));
    copy_cstr(current_player, sizeof(current_player), store.current_player);
}

static void scores_save(void) {
    if (!scores_loaded || scores_nvs_init() != ESP_OK) {
        return;
    }

    score_store_t store;
    memset(&store, 0, sizeof(store));
    store.magic = SCORE_NVS_MAGIC;
    store.version = SCORE_NVS_VERSION;
    store.count = (uint16_t)score_count;
    copy_cstr(store.current_player, sizeof(store.current_player), current_player);
    memcpy(store.entries, scores, sizeof(scores));

    nvs_handle_t nvs;
    if (nvs_open_from_partition(SCORE_NVS_PARTITION,
                                SCORE_NVS_NAMESPACE,
                                NVS_READWRITE,
                                &nvs) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(nvs, "scores", &store, sizeof(store)) == ESP_OK) {
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

int prg32_score_player_get(char *out_player, size_t max_len) {
    if (!out_player || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    scores_load_once();
    copy_cstr(out_player, max_len, current_player);
    return ESP_OK;
}

int prg32_score_player_set(const char *player) {
    if (!player || !player[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    scores_load_once();
    copy_cstr(current_player, sizeof(current_player), player);
    scores_save();
    return ESP_OK;
}

int prg32_score_player_prompt(void) {
    char player[sizeof(current_player)];
    copy_cstr(player, sizeof(player), current_player);
    int len = prg32_text_input(player, sizeof(player), "PLAYER NAME");
    if (len < 0) {
        return len;
    }
    if (player[0]) {
        prg32_score_player_set(player);
    }
    return (int)strlen(current_player);
}

int prg32_score_submit(const char *game, const char *player, uint32_t score) {
    if (!game || !game[0] || !player || !player[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    scores_load_once();

    int insert_at = score_count;
    int game_seen = 0;
    int game_insert_rank = 0;
    for (int i = 0; i < score_count; ++i) {
        if (!score_entry_matches_game(&scores[i], game)) {
            continue;
        }
        if (score <= scores[i].score.score) {
            game_insert_rank++;
            game_seen++;
            continue;
        }
        insert_at = i;
        break;
    }
    if (insert_at == score_count) {
        game_insert_rank = game_seen;
    }

    if (game_insert_rank >= PRG32_SCOREBOARD_TOP_MAX) {
        return ESP_OK;
    }

    int drop_at = -1;
    int game_rank = 0;
    for (int i = 0; i < score_count; ++i) {
        if (!score_entry_matches_game(&scores[i], game)) {
            continue;
        }
        if (game_rank == PRG32_SCOREBOARD_TOP_MAX - 1) {
            drop_at = i;
            break;
        }
        game_rank++;
    }

    if (drop_at >= 0) {
        memmove(&scores[drop_at],
                &scores[drop_at + 1],
                sizeof(scores[0]) * (score_count - drop_at - 1));
        score_count--;
        if (insert_at > drop_at) {
            insert_at--;
        }
    } else if (score_count >= PRG32_SCORE_MAX) {
        score_count--;
        if (insert_at > score_count) {
            insert_at = score_count;
        }
    }

    memmove(&scores[insert_at + 1],
            &scores[insert_at],
            sizeof(scores[0]) * (score_count - insert_at));
    copy_cstr(scores[insert_at].score.game, sizeof(scores[insert_at].score.game), game);
    copy_cstr(scores[insert_at].score.player,
              sizeof(scores[insert_at].score.player),
              player);
    scores[insert_at].score.score = score;
    scores[insert_at].pending_remote = 1;
    if (score_count < PRG32_SCORE_MAX) {
        score_count++;
    }
    scores_save();
    prg32_score_sync_remote();
    return ESP_OK;
}

int prg32_score_submit_current_player(const char *game, uint32_t score) {
    return prg32_score_submit(game, current_player, score);
}

int prg32_score_sync_remote(void) {
    scores_load_once();
#if PRG32_WIFI_ENABLE
    char store_url[128];
    if (resolve_configured_store_url(store_url, sizeof(store_url)) != 0) {
        return 0;
    }

    int synced = 0;
    for (int i = 0; i < score_count; ++i) {
        if (!scores[i].pending_remote) {
            continue;
        }
        if (prg32_score_submit_remote(store_url,
                                      scores[i].score.game,
                                      scores[i].score.player,
                                      scores[i].score.score) == ESP_OK) {
            scores[i].pending_remote = 0;
            synced++;
        }
    }
    if (synced > 0) {
        scores_save();
    }
    return synced;
#else
    return 0;
#endif
}

int prg32_score_reset_local(const char *game) {
    scores_load_once();
    int write = 0;
    int removed = 0;
    for (int read = 0; read < score_count; ++read) {
        if (score_entry_matches_game(&scores[read], game)) {
            removed++;
            continue;
        }
        if (write != read) {
            scores[write] = scores[read];
        }
        write++;
    }
    if (removed > 0) {
        memset(&scores[write], 0, sizeof(scores[0]) * (PRG32_SCORE_MAX - write));
        score_count = write;
        scores_save();
    }
    return removed;
}

int prg32_score_count(const char *game) {
    scores_load_once();
    int count = 0;
    for (int i = 0; i < score_count; ++i) {
        if (score_entry_matches_game(&scores[i], game)) {
            count++;
        }
    }
    return count;
}

int prg32_score_get(const char *game, int index, prg32_score_t *out_score) {
    if (!out_score || index < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    scores_load_once();
    int raw = score_visible_index(game, index);
    if (raw < 0) {
        return ESP_FAIL;
    }
    *out_score = scores[raw].score;
    return ESP_OK;
}

int prg32_scoreboard_show(const char *game, const char *title) {
    scores_load_once();
    prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT);
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if (input & (PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT)) {
            prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT);
            return 0;
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, title ? title : "SCOREBOARD", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8,
                        24,
                        game && game[0] ? game : "ALL GAMES",
                        PRG32_COLOR_CYAN,
                        0);

        int shown = prg32_score_count(game);
        if (shown == 0) {
            prg32_gfx_text8(8, 64, "NO SCORES YET", PRG32_COLOR_YELLOW, 0);
        }
        if (shown > PRG32_SCOREBOARD_TOP_MAX) {
            shown = PRG32_SCOREBOARD_TOP_MAX;
        }
        for (int row = 0; row < shown; ++row) {
            prg32_score_t score;
            if (prg32_score_get(game, row, &score) != ESP_OK) {
                break;
            }
            char line[48];
            snprintf(line,
                     sizeof(line),
                     "%2d %-16s %lu",
                     row + 1,
                     score.player,
                     (unsigned long)score.score);
            prg32_gfx_text8(8, 48 + row * 18, line, PRG32_COLOR_WHITE, 0);
        }

        prg32_gfx_text8(8, 224, "A / B / SELECT BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

#if PRG32_WIFI_ENABLE

static int resolve_configured_store_url(char *out_url, size_t max_len) {
    if (!out_url || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (prg32_store_url_get(out_url, max_len) == 0) {
        return ESP_OK;
    }
    if (CONFIG_PRG32_STORE_URL[0]) {
        copy_cstr(out_url, max_len, CONFIG_PRG32_STORE_URL);
        return ESP_OK;
    }
    out_url[0] = '\0';
    return ESP_FAIL;
}

static int copy_json_string(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0 || !src) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t out = 0;
    for (size_t in = 0; src[in]; ++in) {
        unsigned char ch = (unsigned char)src[in];
        const char *escape = NULL;
        if (ch == '"' || ch == '\\') {
            escape = ch == '"' ? "\\\"" : "\\\\";
        } else if (ch < 32) {
            ch = '?';
        }
        if (escape) {
            if (out + 2 >= dst_size) {
                return ESP_ERR_INVALID_ARG;
            }
            dst[out++] = escape[0];
            dst[out++] = escape[1];
        } else {
            if (out + 1 >= dst_size) {
                return ESP_ERR_INVALID_ARG;
            }
            dst[out++] = (char)ch;
        }
    }
    dst[out] = '\0';
    return ESP_OK;
}

int prg32_score_submit_remote(const char *base_url,
                              const char *game,
                              const char *player,
                              uint32_t score) {
    if (!base_url || !game || !player) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[160];
    int n = snprintf(url, sizeof(url), "%s/api/scores", base_url);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }

    char game_json[40];
    char player_json[40];
    if (copy_json_string(game_json, sizeof(game_json), game) != ESP_OK ||
        copy_json_string(player_json, sizeof(player_json), player) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    char body[160];
    n = snprintf(body,
                 sizeof(body),
                 "{\"game\":\"%s\",\"player\":\"%s\",\"score\":%lu}",
                 game_json,
                 player_json,
                 (unsigned long)score);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            err = ESP_FAIL;
        }
    }
    esp_http_client_cleanup(client);
    return err;
}

#if PRG32_WIFI_SCORES_ENABLE
static esp_err_t get_scores(httpd_req_t *req) {
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        httpd_resp_send_err(req, 500, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < score_count; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, 500, "out of memory");
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(o, "game", scores[i].score.game);
        cJSON_AddStringToObject(o, "player", scores[i].score.player);
        cJSON_AddNumberToObject(o, "score", scores[i].score.score);
        cJSON_AddItemToArray(root, o);
    }
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    if (json) {
        httpd_resp_sendstr(req, json);
        cJSON_free(json);
    } else {
        httpd_resp_sendstr(req, "[]");
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t read_request_body(httpd_req_t *req, char *body, size_t size) {
    if (!body || size == 0 || req->content_len <= 0) {
        httpd_resp_send_err(req, 400, "empty body");
        return ESP_FAIL;
    }
    if ((size_t)req->content_len >= size) {
        httpd_resp_send_err(req, 400, "body too large");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int n = httpd_req_recv(req,
                               body + received,
                               req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            httpd_resp_send_err(req, 400, "failed to read body");
            return ESP_FAIL;
        }
        received += (size_t)n;
    }
    body[received] = '\0';
    return ESP_OK;
}

static esp_err_t post_score(httpd_req_t *req) {
    char body[192];
    if (read_request_body(req, body, sizeof(body)) != ESP_OK) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, 400, "invalid json");
        return ESP_FAIL;
    }
    cJSON *game = cJSON_GetObjectItem(root, "game");
    cJSON *player = cJSON_GetObjectItem(root, "player");
    cJSON *score = cJSON_GetObjectItem(root, "score");
    if (!cJSON_IsString(game) ||
        !cJSON_IsString(player) ||
        !cJSON_IsNumber(score) ||
        score->valuedouble < 0.0 ||
        score->valuedouble > (double)UINT32_MAX) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, 400, "expected game, player, score");
        return ESP_FAIL;
    }
    int err = prg32_score_submit(game->valuestring,
                                 player->valuestring,
                                 (uint32_t)score->valuedouble);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, 400, "invalid score");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}
#endif

void prg32_http_register_score_handlers(httpd_handle_t server) {
#if PRG32_WIFI_SCORES_ENABLE
    httpd_uri_t gs = {
        .uri = "/api/scores",
        .method = HTTP_GET,
        .handler = get_scores,
    };
    httpd_uri_t ps = {
        .uri = "/api/scores",
        .method = HTTP_POST,
        .handler = post_score,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &gs));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ps));
#else
    (void)server;
#endif
}

#else

int prg32_score_submit_remote(const char *base_url,
                              const char *game,
                              const char *player,
                              uint32_t score) {
    (void)base_url;
    (void)game;
    (void)player;
    (void)score;
    return -1;
}

void prg32_http_register_score_handlers(void *server) {
    (void)server;
}

#endif
