#include "prg32.h"
#include "prg32_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if !CONFIG_PRG32_DISPLAY_QEMU_RGB
#include "mdns.h"
#endif

#ifndef CONFIG_PRG32_STORE_URL
#define CONFIG_PRG32_STORE_URL ""
#endif

#define STORE_NAMESPACE "prg32"
#define STORE_URL_KEY "store_url"
#define STORE_DISCOVERY_ABI "prg32-store-discovery-1.0"
#define STORE_CACHE_MS 60000

static const char *TAG = "prg32_store";
static char cached_url[PRG32_STORE_URL_MAX_LEN];
static uint32_t cached_at_ms;

static int copy_url(char *out_url, size_t max_len, const char *url) {
    if (!out_url || max_len == 0 || !url || !url[0]) {
        return -1;
    }
    size_t len = strnlen(url, PRG32_STORE_URL_MAX_LEN);
    if (len + 1 > max_len) {
        return -1;
    }
    memcpy(out_url, url, len);
    out_url[len] = '\0';
    return 0;
}

static void clear_cache(void) {
    cached_url[0] = '\0';
    cached_at_ms = 0;
}

int prg32_store_url_get(char *out_url, size_t max_len) {
    if (!out_url || max_len == 0) {
        return -1;
    }
    out_url[0] = '\0';
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return -1;
    }
    size_t required = max_len;
    err = nvs_get_str(handle, STORE_URL_KEY, out_url, &required);
    nvs_close(handle);
    return err == ESP_OK && out_url[0] ? 0 : -1;
}

int prg32_store_url_set(const char *url) {
    if (!url || !url[0] || strnlen(url, PRG32_STORE_URL_MAX_LEN) >= PRG32_STORE_URL_MAX_LEN) {
        return -1;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return -1;
    }
    err = nvs_set_str(handle, STORE_URL_KEY, url);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    clear_cache();
    return err == ESP_OK ? 0 : -1;
}

void prg32_store_url_clear(void) {
    nvs_handle_t handle;
    if (nvs_open(STORE_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, STORE_URL_KEY);
        nvs_commit(handle);
        nvs_close(handle);
    }
    clear_cache();
}

int prg32_store_url_resolve(char *out_url, size_t max_len) {
    uint32_t now = prg32_ticks_ms();
    if (cached_url[0] && (now - cached_at_ms) < STORE_CACHE_MS) {
        return copy_url(out_url, max_len, cached_url);
    }
    if (prg32_store_url_get(out_url, max_len) == 0) {
        copy_url(cached_url, sizeof(cached_url), out_url);
        cached_at_ms = now;
        return 0;
    }
    if (CONFIG_PRG32_STORE_URL[0]) {
        if (copy_url(out_url, max_len, CONFIG_PRG32_STORE_URL) == 0) {
            copy_url(cached_url, sizeof(cached_url), out_url);
            cached_at_ms = now;
            return 0;
        }
    }
    if (prg32_store_discover(out_url, max_len) == 0) {
        copy_url(cached_url, sizeof(cached_url), out_url);
        cached_at_ms = now;
        return 0;
    }
    return -1;
}

int prg32_store_discover(char *out_url, size_t max_len) {
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
    (void)out_url;
    (void)max_len;
    ESP_LOGI(TAG, "mDNS unavailable in QEMU builds; configure CONFIG_PRG32_STORE_URL");
    return -1;
#else
    if (!out_url || max_len == 0) {
        return -1;
    }
    out_url[0] = '\0';
    esp_err_t init_err = mdns_init();
    if (init_err != ESP_OK && init_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "mDNS init failed: %s", esp_err_to_name(init_err));
        return -1;
    }
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(PRG32_STORE_MDNS_SERVICE,
                                   PRG32_STORE_MDNS_PROTO,
                                   PRG32_STORE_MDNS_TIMEOUT_MS,
                                   1,
                                   &results);
    if (err != ESP_OK || !results) {
        ESP_LOGI(TAG, "mDNS store discovery found no service");
        return -1;
    }
    mdns_ip_addr_t *addr = results->addr;
    int rc = -1;
    if (addr && addr->addr.type == ESP_IPADDR_TYPE_V4) {
        char ip[16];
        snprintf(ip,
                 sizeof(ip),
                 IPSTR,
                 IP2STR(&addr->addr.u_addr.ip4));
        uint16_t port = results->port ? results->port : PRG32_STORE_DEFAULT_PORT;
        if (snprintf(out_url, max_len, "http://%s:%u", ip, port) < (int)max_len) {
            ESP_LOGI(TAG, "mDNS discovered CartridgeStore at %s", out_url);
            rc = 0;
        }
    }
    mdns_query_results_free(results);
    return rc;
#endif
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} http_capture_t;

static esp_err_t capture_http(esp_http_client_event_t *evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data) {
        return ESP_OK;
    }
    http_capture_t *capture = (http_capture_t *)evt->user_data;
    size_t room = capture->cap > capture->len ? capture->cap - capture->len - 1 : 0;
    size_t take = evt->data_len < (int)room ? (size_t)evt->data_len : room;
    if (take > 0) {
        memcpy(capture->buf + capture->len, evt->data, take);
        capture->len += take;
        capture->buf[capture->len] = '\0';
    }
    return ESP_OK;
}

static int json_value(const char *json, const char *key, char *out, size_t out_len) {
    if (!json || !key || !out || out_len == 0) {
        return -1;
    }
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(needle), ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_len) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0 ? 0 : -1;
}

int prg32_store_ping(const char *base_url, char *out_name, size_t name_len) {
    if (!base_url || !base_url[0]) {
        return -1;
    }
    char url[PRG32_STORE_URL_MAX_LEN + 40];
    snprintf(url, sizeof(url), "%s/.well-known/prg32-store.json", base_url);
    char body[512] = {0};
    http_capture_t capture = {.buf = body, .cap = sizeof(body), .len = 0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = PRG32_STORE_HTTP_TIMEOUT_MS,
        .event_handler = capture_http,
        .user_data = &capture,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return -1;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGI(TAG, "store ping failed status=%d err=%s", status, esp_err_to_name(err));
        return -1;
    }
    char abi[40];
    if (json_value(body, "abi", abi, sizeof(abi)) != 0 ||
        strcmp(abi, STORE_DISCOVERY_ABI) != 0) {
        ESP_LOGI(TAG, "store discovery ABI mismatch");
        return -1;
    }
    if (out_name && name_len > 0) {
        out_name[0] = '\0';
        json_value(body, "name", out_name, name_len);
    }
    return 0;
}
