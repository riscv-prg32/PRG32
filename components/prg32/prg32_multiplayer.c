#include "prg32.h"
#include "prg32_config.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_PRG32_DISPLAY_QEMU_RGB
#define CONFIG_PRG32_DISPLAY_QEMU_RGB 0
#endif

#ifndef PRG32_MULTIPLAYER_ENABLE
#define PRG32_MULTIPLAYER_ENABLE 1
#endif

#ifndef PRG32_MULTIPLAYER_TRANSPORT_ENABLE
#define PRG32_MULTIPLAYER_TRANSPORT_ENABLE PRG32_MULTIPLAYER_ENABLE
#endif

#ifndef PRG32_MULTIPLAYER_SERVER_URL
#define PRG32_MULTIPLAYER_SERVER_URL "ws://192.168.4.2:8081"
#endif

#ifndef PRG32_MULTIPLAYER_SEND_PERIOD_MS
#define PRG32_MULTIPLAYER_SEND_PERIOD_MS 50
#endif

#ifndef PRG32_MULTIPLAYER_PEER_TIMEOUT_MS
#define PRG32_MULTIPLAYER_PEER_TIMEOUT_MS 3000
#endif

#define PRG32_MP_SIGNATURE_LEN 48

typedef struct {
    uint8_t initialized;
    uint8_t joined;
    uint32_t flags;
    uint32_t player_id;
    uint32_t last_send_ms;
    char signature[PRG32_MP_SIGNATURE_LEN];
    prg32_player_state_t local;
    prg32_player_state_t peers[PRG32_MP_MAX_PEERS];
    int peer_count;
} prg32_mp_state_t;

static prg32_mp_state_t g_mp;

static void ensure_mp_state(void);

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int valid_signature(const char *signature) {
    if (!signature || signature[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; signature[i] != '\0'; ++i) {
        char ch = signature[i];
        if (i >= PRG32_MP_SIGNATURE_LEN - 1) {
            return 0;
        }
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '.' || ch == ':')) {
            return 0;
        }
    }
    return 1;
}

static void clear_peers(void) {
    ensure_mp_state();
    memset(g_mp.peers, 0, sizeof(g_mp.peers));
    g_mp.peer_count = 0;
}

static void prune_peers(uint32_t now_ms) {
    ensure_mp_state();
    int out = 0;
    for (int i = 0; i < g_mp.peer_count; ++i) {
        prg32_player_state_t peer = g_mp.peers[i];
        if ((uint32_t)(now_ms - peer.last_seen_ms) <=
            PRG32_MULTIPLAYER_PEER_TIMEOUT_MS) {
            g_mp.peers[out++] = peer;
        }
    }
    g_mp.peer_count = out;
}

static void update_peer(const prg32_player_state_t *peer) {
    if (!peer || peer->player_id == 0 || peer->player_id == g_mp.player_id) {
        return;
    }
    for (int i = 0; i < g_mp.peer_count; ++i) {
        if (g_mp.peers[i].player_id == peer->player_id) {
            g_mp.peers[i] = *peer;
            return;
        }
    }
    if (g_mp.peer_count < PRG32_MP_MAX_PEERS) {
        g_mp.peers[g_mp.peer_count++] = *peer;
    }
}

static void remove_peer(uint32_t player_id) {
    int out = 0;
    for (int i = 0; i < g_mp.peer_count; ++i) {
        if (g_mp.peers[i].player_id != player_id) {
            g_mp.peers[out++] = g_mp.peers[i];
        }
    }
    g_mp.peer_count = out;
}

#if CONFIG_PRG32_DISPLAY_QEMU_RGB || !PRG32_MULTIPLAYER_TRANSPORT_ENABLE

void prg32_multiplayer_init(void) {
    if (!g_mp.initialized) {
        memset(&g_mp, 0, sizeof(g_mp));
        g_mp.initialized = 1;
        g_mp.player_id = 1;
        g_mp.local.player_id = g_mp.player_id;
    }
}

static void ensure_mp_state(void) {
    prg32_multiplayer_init();
}

bool prg32_multiplayer_available(void) {
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
    return true;
#else
    return false;
#endif
}

int prg32_multiplayer_join(const char *cartridge_signature, uint32_t flags) {
    ensure_mp_state();
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
    if (!valid_signature(cartridge_signature)) {
        return -1;
    }
    g_mp.joined = 1;
    g_mp.flags = flags;
    copy_text(g_mp.signature, sizeof(g_mp.signature), cartridge_signature);
    clear_peers();
    return 0;
#else
    (void)cartridge_signature;
    (void)flags;
    return -1;
#endif
}

int prg32_multiplayer_leave(void) {
    ensure_mp_state();
    g_mp.joined = 0;
    clear_peers();
    return 0;
}

void prg32_multiplayer_tick(void) {
    ensure_mp_state();
    prune_peers(prg32_ticks_ms());
}

int prg32_multiplayer_set_local_state(int16_t x,
                                      int16_t y,
                                      uint16_t sprite,
                                      uint16_t flags) {
    ensure_mp_state();
    if (!g_mp.joined) {
        return -1;
    }
    g_mp.local.player_id = g_mp.player_id;
    g_mp.local.x = x;
    g_mp.local.y = y;
    g_mp.local.sprite = sprite;
    g_mp.local.flags = flags;
    g_mp.local.frame++;
    g_mp.local.last_seen_ms = prg32_ticks_ms();
    return 0;
}

int prg32_multiplayer_set_input(uint32_t input) {
    ensure_mp_state();
    if (!g_mp.joined) {
        return -1;
    }
    g_mp.local.input = input & 0x7fu;
    return 0;
}

int prg32_multiplayer_get_peer_count(void) {
    ensure_mp_state();
    prune_peers(prg32_ticks_ms());
    return g_mp.peer_count;
}

int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out) {
    ensure_mp_state();
    prune_peers(prg32_ticks_ms());
    if (!out || index < 0 || index >= g_mp.peer_count) {
        return -1;
    }
    *out = g_mp.peers[index];
    return 0;
}

#else

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "prg32_mp";
static esp_websocket_client_handle_t g_ws;
static SemaphoreHandle_t g_mp_lock;
static uint8_t g_connected;

static int lock_mp(void) {
    if (!g_mp_lock) {
        return 0;
    }
    return xSemaphoreTake(g_mp_lock, portMAX_DELAY) == pdTRUE ? 0 : -1;
}

static void unlock_mp(void) {
    if (g_mp_lock) {
        xSemaphoreGive(g_mp_lock);
    }
}

static int json_int(cJSON *root, const char *name, int fallback) {
    cJSON *item = cJSON_GetObjectItem(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static void send_join(void) {
    if (!g_ws || !g_connected || !g_mp.joined) {
        return;
    }
    char msg[192];
    int n = snprintf(msg,
                     sizeof(msg),
                     "{\"type\":\"join\",\"signature\":\"%s\","
                     "\"flags\":%lu,\"player_id\":%lu}",
                     g_mp.signature,
                     (unsigned long)g_mp.flags,
                     (unsigned long)g_mp.player_id);
    if (n > 0 && n < (int)sizeof(msg)) {
        esp_websocket_client_send_text(g_ws, msg, n, 0);
    }
}

static void send_leave(void) {
    if (!g_ws || !g_connected || !g_mp.joined) {
        return;
    }
    char msg[96];
    int n = snprintf(msg,
                     sizeof(msg),
                     "{\"type\":\"leave\",\"player_id\":%lu}",
                     (unsigned long)g_mp.player_id);
    if (n > 0 && n < (int)sizeof(msg)) {
        esp_websocket_client_send_text(g_ws, msg, n, 0);
    }
}

static void send_state(void) {
    if (!g_ws || !g_connected || !g_mp.joined) {
        return;
    }
    prg32_player_state_t local = g_mp.local;
    local.player_id = g_mp.player_id;
    char msg[192];
    int n = snprintf(msg,
                     sizeof(msg),
                     "{\"type\":\"state\",\"player_id\":%lu,"
                     "\"x\":%d,\"y\":%d,\"sprite\":%u,\"flags\":%u,"
                     "\"input\":%lu,\"frame\":%lu}",
                     (unsigned long)local.player_id,
                     (int)local.x,
                     (int)local.y,
                     (unsigned)local.sprite,
                     (unsigned)local.flags,
                     (unsigned long)local.input,
                     (unsigned long)local.frame);
    if (n > 0 && n < (int)sizeof(msg)) {
        esp_websocket_client_send_text(g_ws, msg, n, 0);
    }
}

static void handle_peer_json(cJSON *root) {
    prg32_player_state_t peer = {
        .player_id = (uint32_t)json_int(root, "player_id", 0),
        .x = (int16_t)json_int(root, "x", 0),
        .y = (int16_t)json_int(root, "y", 0),
        .sprite = (uint16_t)json_int(root, "sprite", 0),
        .flags = (uint16_t)json_int(root, "flags", 0),
        .input = (uint32_t)json_int(root, "input", 0),
        .frame = (uint32_t)json_int(root, "frame", 0),
        .last_seen_ms = prg32_ticks_ms(),
    };
    if (lock_mp() == 0) {
        update_peer(&peer);
        unlock_mp();
    }
}

static void handle_ws_text(const char *data, int len) {
    if (!data || len <= 0 || len > 255) {
        return;
    }
    char buf[256];
    memcpy(buf, data, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring) {
        if (strcmp(type->valuestring, "welcome") == 0) {
            uint32_t player_id =
                (uint32_t)json_int(root, "player_id", (int)g_mp.player_id);
            if (player_id != 0 && lock_mp() == 0) {
                g_mp.player_id = player_id;
                g_mp.local.player_id = player_id;
                unlock_mp();
            }
        } else if (strcmp(type->valuestring, "peer") == 0) {
            handle_peer_json(root);
        } else if (strcmp(type->valuestring, "leave") == 0) {
            uint32_t player_id = (uint32_t)json_int(root, "player_id", 0);
            if (lock_mp() == 0) {
                remove_peer(player_id);
                unlock_mp();
            }
        }
    }
    cJSON_Delete(root);
}

static void websocket_event(void *handler_args,
                            esp_event_base_t base,
                            int32_t event_id,
                            void *event_data) {
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data =
        (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            g_connected = 1;
            send_join();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            g_connected = 0;
            break;
        case WEBSOCKET_EVENT_DATA:
            handle_ws_text(data->data_ptr, data->data_len);
            break;
        case WEBSOCKET_EVENT_ERROR:
            g_connected = 0;
            break;
        default:
            break;
    }
}

static int ensure_network(void) {
    prg32_wifi_mode_t mode = prg32_wifi_current_mode();
    if (mode == PRG32_WIFI_MODE_STA || mode == PRG32_WIFI_MODE_APSTA) {
        return 0;
    }

    prg32_wifi_config_t config = {
#if PRG32_WIFI_AP_ENABLE
        .mode = PRG32_WIFI_MODE_APSTA,
#else
        .mode = PRG32_WIFI_MODE_STA,
#endif
    };
    copy_text(config.ssid, sizeof(config.ssid), PRG32_WIFI_SSID);
    copy_text(config.password, sizeof(config.password), PRG32_WIFI_PASSWORD);
    copy_text(config.ap_ssid, sizeof(config.ap_ssid), PRG32_WIFI_AP_SSID);
    copy_text(config.ap_password,
              sizeof(config.ap_password),
              PRG32_WIFI_AP_PASSWORD);
    return prg32_wifi_start_mode(&config);
}

static int ensure_client(void) {
    if (g_ws) {
        return 0;
    }
    esp_websocket_client_config_t cfg = {
        .uri = PRG32_MULTIPLAYER_SERVER_URL,
        .reconnect_timeout_ms = 1000,
        .network_timeout_ms = 1000,
    };
    g_ws = esp_websocket_client_init(&cfg);
    if (!g_ws) {
        return -1;
    }
    ESP_ERROR_CHECK(esp_websocket_register_events(g_ws,
                                                  WEBSOCKET_EVENT_ANY,
                                                  websocket_event,
                                                  NULL));
    esp_err_t err = esp_websocket_client_start(g_ws);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(g_ws);
        g_ws = NULL;
        return -1;
    }
    return 0;
}

void prg32_multiplayer_init(void) {
    if (!g_mp_lock) {
        g_mp_lock = xSemaphoreCreateMutex();
    }
    if (!g_mp.initialized) {
        memset(&g_mp, 0, sizeof(g_mp));
        g_mp.initialized = 1;
        g_mp.player_id = esp_random();
        if (g_mp.player_id == 0) {
            g_mp.player_id = 1;
        }
        g_mp.local.player_id = g_mp.player_id;
    }
}

static void ensure_mp_state(void) {
    prg32_multiplayer_init();
}

bool prg32_multiplayer_available(void) {
    return PRG32_MULTIPLAYER_ENABLE != 0;
}

int prg32_multiplayer_join(const char *cartridge_signature, uint32_t flags) {
    ensure_mp_state();
    if (!valid_signature(cartridge_signature) || !prg32_multiplayer_available()) {
        return -1;
    }
    if (ensure_network() != 0 || ensure_client() != 0) {
        return -1;
    }
    if (lock_mp() != 0) {
        return -1;
    }
    g_mp.joined = 1;
    g_mp.flags = flags;
    copy_text(g_mp.signature, sizeof(g_mp.signature), cartridge_signature);
    clear_peers();
    unlock_mp();
    send_join();
    return 0;
}

int prg32_multiplayer_leave(void) {
    ensure_mp_state();
    send_leave();
    if (lock_mp() == 0) {
        g_mp.joined = 0;
        clear_peers();
        unlock_mp();
    }
    return 0;
}

void prg32_multiplayer_tick(void) {
    ensure_mp_state();
    uint32_t now = prg32_ticks_ms();
    if (lock_mp() == 0) {
        prune_peers(now);
        unlock_mp();
    }
    if (!g_mp.joined ||
        (uint32_t)(now - g_mp.last_send_ms) < PRG32_MULTIPLAYER_SEND_PERIOD_MS) {
        return;
    }
    g_mp.last_send_ms = now;
    send_state();
}

int prg32_multiplayer_set_local_state(int16_t x,
                                      int16_t y,
                                      uint16_t sprite,
                                      uint16_t flags) {
    ensure_mp_state();
    if (lock_mp() != 0) {
        return -1;
    }
    if (!g_mp.joined) {
        unlock_mp();
        return -1;
    }
    g_mp.local.player_id = g_mp.player_id;
    g_mp.local.x = x;
    g_mp.local.y = y;
    g_mp.local.sprite = sprite;
    g_mp.local.flags = flags;
    g_mp.local.frame++;
    g_mp.local.last_seen_ms = prg32_ticks_ms();
    unlock_mp();
    return 0;
}

int prg32_multiplayer_set_input(uint32_t input) {
    ensure_mp_state();
    if (lock_mp() != 0) {
        return -1;
    }
    if (!g_mp.joined) {
        unlock_mp();
        return -1;
    }
    g_mp.local.input = input & 0x7fu;
    unlock_mp();
    return 0;
}

int prg32_multiplayer_get_peer_count(void) {
    ensure_mp_state();
    int count = 0;
    if (lock_mp() == 0) {
        prune_peers(prg32_ticks_ms());
        count = g_mp.peer_count;
        unlock_mp();
    }
    return count;
}

int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out) {
    ensure_mp_state();
    if (!out || lock_mp() != 0) {
        return -1;
    }
    prune_peers(prg32_ticks_ms());
    if (index < 0 || index >= g_mp.peer_count) {
        unlock_mp();
        return -1;
    }
    *out = g_mp.peers[index];
    unlock_mp();
    return 0;
}

#endif
