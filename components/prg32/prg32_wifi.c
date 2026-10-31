#include "prg32.h"
#include "prg32_config.h"

#if PRG32_WIFI_ENABLE

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PRG32_WIFI_SCAN_VISIBLE
#define PRG32_WIFI_SCAN_VISIBLE 8
#endif

#ifndef PRG32_WIFI_STA_LEGACY_PROTOCOLS
#define PRG32_WIFI_STA_LEGACY_PROTOCOLS 0
#endif

static const char *TAG = "prg32_wifi";
static bool wifi_started;
static bool wifi_initialized;
static bool sta_autoconnect = true;
static prg32_wifi_mode_t active_mode = PRG32_WIFI_MODE_OFF;
static esp_netif_t *sta_netif;
static esp_netif_t *ap_netif;
static char active_ssid[32];
static char active_ip[16] = "-";
static char active_status[32] = "idle";
static wifi_auth_mode_t selected_authmode = WIFI_AUTH_OPEN;
static char selected_ssid[32];
static uint8_t selected_bssid[6];
static uint8_t selected_channel;
static bool selected_ap_valid;
static bool selected_ap_locked;

/* The most recently connected infrastructure networks, newest first. They are
 * stored as one NVS blob, "known", in the prg32wifi namespace. */
typedef struct {
    char ssid[32];
    char password[64];
} known_network_t;
static known_network_t known_networks[PRG32_WIFI_KNOWN_MAX];
static int known_count;
static bool known_loaded;

/* The setup menu (main task) and the device protocol (its own task) both
 * start, stop, and scan Wi-Fi, so those operations are serialized. The lock
 * is recursive because connecting restarts Wi-Fi through the same API. */
static SemaphoreHandle_t wifi_lock;
static portMUX_TYPE wifi_lock_init_mux = portMUX_INITIALIZER_UNLOCKED;

static void wifi_lock_take(void) {
    if (!wifi_lock) {
        /* Create outside the critical section (creating a mutex may not run
         * with interrupts disabled); only publishing the handle is guarded,
         * and a task that loses the race deletes its spare mutex. */
        SemaphoreHandle_t created = xSemaphoreCreateRecursiveMutex();
        bool published = false;
        portENTER_CRITICAL(&wifi_lock_init_mux);
        if (!wifi_lock) {
            wifi_lock = created;
            published = true;
        }
        portEXIT_CRITICAL(&wifi_lock_init_mux);
        if (!published && created) {
            vSemaphoreDelete(created);
        }
    }
    xSemaphoreTakeRecursive(wifi_lock, portMAX_DELAY);
}

static void wifi_lock_give(void) {
    xSemaphoreGiveRecursive(wifi_lock);
}

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

static const char *disconnect_reason_name(uint8_t reason) {
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
        return "AUTH EXPIRED";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "HANDSHAKE TIMEOUT";
    case WIFI_REASON_802_1X_AUTH_FAILED:
    case WIFI_REASON_AUTH_FAIL:
        return "AUTH FAILED";
    case WIFI_REASON_ASSOC_FAIL:
        return "ASSOC FAILED";
    case WIFI_REASON_NO_AP_FOUND:
        return "NO AP FOUND";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "SECURITY MISMATCH";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "AUTHMODE MISMATCH";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "RSSI TOO LOW";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "BEACON TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL:
        return "CONNECTION FAILED";
    default:
        return "DISCONNECTED";
    }
}

static const char *auth_mode_name(wifi_auth_mode_t mode) {
    switch (mode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-ENT";
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case WIFI_AUTH_WPA3_ENTERPRISE:
        return "WPA3-ENT";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        return "ENT";
#endif
    default:
        return "SECURE";
    }
}

static bool auth_mode_is_enterprise(wifi_auth_mode_t mode) {
    if (mode == WIFI_AUTH_WPA2_ENTERPRISE) {
        return true;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    return mode == WIFI_AUTH_WPA3_ENTERPRISE ||
           mode == WIFI_AUTH_WPA2_WPA3_ENTERPRISE;
#else
    return false;
#endif
}

static const char *auth_mode_short_name(wifi_auth_mode_t mode) {
    switch (mode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/W2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "W2/W3";
    case WIFI_AUTH_WPA2_ENTERPRISE:
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
#endif
        return "ENT";
    default:
        return "SEC";
    }
}

static void known_load(void) {
    known_count = 0;
    known_loaded = true;
    nvs_handle_t nvs;
    if (nvs_open("prg32wifi", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    size_t size = sizeof(known_networks);
    if (nvs_get_blob(nvs, "known", known_networks, &size) == ESP_OK &&
        size % sizeof(known_network_t) == 0) {
        known_count = (int)(size / sizeof(known_network_t));
    }
    nvs_close(nvs);
    for (int i = 0; i < known_count; ++i) {
        known_networks[i].ssid[sizeof(known_networks[i].ssid) - 1] = '\0';
        known_networks[i].password[sizeof(known_networks[i].password) - 1] = '\0';
    }
}

static void known_save(void) {
    nvs_handle_t nvs;
    if (nvs_open("prg32wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (known_count > 0) {
        nvs_set_blob(nvs, "known", known_networks,
                     (size_t)known_count * sizeof(known_network_t));
    } else {
        nvs_erase_key(nvs, "known");
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

static int known_find(const char *ssid) {
    for (int i = 0; i < known_count; ++i) {
        if (strcmp(known_networks[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

static void known_remove(int index) {
    if (index < 0 || index >= known_count) {
        return;
    }
    for (int i = index; i + 1 < known_count; ++i) {
        known_networks[i] = known_networks[i + 1];
    }
    known_count--;
    /* Leave no copy of the removed password in RAM. */
    memset(&known_networks[known_count], 0, sizeof(known_networks[0]));
    known_save();
}

/* Put a network that just connected first, dropping the oldest when full. */
static void known_remember(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0') {
        return;
    }
    int index = known_find(ssid);
    if (index == 0 &&
        strcmp(known_networks[0].password, password ? password : "") == 0) {
        return; /* already first and unchanged: no flash write */
    }
    if (index < 0) {
        index = known_count < PRG32_WIFI_KNOWN_MAX ? known_count
                                                   : PRG32_WIFI_KNOWN_MAX - 1;
        if (known_count < PRG32_WIFI_KNOWN_MAX) {
            known_count++;
        }
    }
    for (int i = index; i > 0; --i) {
        known_networks[i] = known_networks[i - 1];
    }
    memset(&known_networks[0], 0, sizeof(known_networks[0]));
    copy_cstr(known_networks[0].ssid, sizeof(known_networks[0].ssid), ssid);
    copy_cstr(known_networks[0].password,
              sizeof(known_networks[0].password),
              password);
    known_save();
    ESP_LOGI(TAG, "remembered network %s (%d stored)", ssid, known_count);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && sta_autoconnect) {
        copy_cstr(active_status, sizeof(active_status), "CONNECTING");
        esp_wifi_connect();
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && sta_autoconnect) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)data;
        snprintf(active_ip, sizeof(active_ip), "connecting");
        copy_cstr(active_status,
                  sizeof(active_status),
                  disconnect_reason_name(event ? event->reason : 0));
        ESP_LOGW(TAG,
                 "Wi-Fi disconnected reason=%u (%s), reconnecting",
                 event ? (unsigned)event->reason : 0,
                 active_status);
        vTaskDelay(pdMS_TO_TICKS(1000));
        /* A scan may have started during the wait; connecting would end it. */
        if (sta_autoconnect) {
            esp_wifi_connect();
        }
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(active_ip, sizeof(active_ip), IPSTR, IP2STR(&event->ip_info.ip));
        copy_cstr(active_status, sizeof(active_status), "CONNECTED");
        ESP_LOGI(TAG, "Wi-Fi connected ip=%s", active_ip);
    }
}

static esp_err_t wifi_stack_init_locked(void) {
    if (wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");
    /* The driver's "mode : sta (MAC)" line would log the factory MAC, which the
     * multiplayer protocol uses as the identity source and forbids logging. This
     * file logs connection progress itself; driver warnings still appear. */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "event loop failed");
    }

    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
#ifdef PRG32_WIFI_COUNTRY_CODE
    esp_err_t country_err = esp_wifi_set_country_code(PRG32_WIFI_COUNTRY_CODE, true);
    if (country_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Wi-Fi country %s failed: %s",
                 PRG32_WIFI_COUNTRY_CODE,
                 esp_err_to_name(country_err));
    }
#endif
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               wifi_event,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               wifi_event,
                                               NULL));
    known_load();
    wifi_initialized = true;
    return ESP_OK;
}

static esp_err_t wifi_stack_init(void) {
    wifi_lock_take();
    esp_err_t err = wifi_stack_init_locked();
    wifi_lock_give();
    return err;
}

static void configure_sta_protocols(void) {
#if PRG32_WIFI_STA_LEGACY_PROTOCOLS
    esp_err_t err = esp_wifi_set_protocol(WIFI_IF_STA,
                                          WIFI_PROTOCOL_11B |
                                          WIFI_PROTOCOL_11G |
                                          WIFI_PROTOCOL_11N);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "STA protocol set failed: %s", esp_err_to_name(err));
    }
#endif
}

static void default_config(prg32_wifi_config_t *config) {
    memset(config, 0, sizeof(*config));
#if PRG32_WIFI_STA_ENABLE
    config->mode = PRG32_WIFI_MODE_STA;
#elif PRG32_WIFI_AP_ENABLE
    config->mode = PRG32_WIFI_MODE_AP;
#else
    config->mode = PRG32_WIFI_MODE_OFF;
#endif
    copy_cstr(config->ssid, sizeof(config->ssid), PRG32_WIFI_SSID);
    copy_cstr(config->password, sizeof(config->password), PRG32_WIFI_PASSWORD);
    copy_cstr(config->ap_ssid, sizeof(config->ap_ssid), PRG32_WIFI_AP_SSID);
    copy_cstr(config->ap_password,
              sizeof(config->ap_password),
              PRG32_WIFI_AP_PASSWORD);
}

static void save_config(const prg32_wifi_config_t *config) {
    nvs_handle_t nvs;
    if (!config || nvs_open("prg32wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_u8(nvs, "mode", (uint8_t)config->mode);
    nvs_set_str(nvs, "ssid", config->ssid);
    nvs_set_str(nvs, "password", config->password);
    nvs_set_str(nvs, "ap_ssid", config->ap_ssid);
    nvs_set_str(nvs, "ap_password", config->ap_password);
    if (config->mode == PRG32_WIFI_MODE_STA &&
        selected_ap_valid &&
        strcmp(config->ssid, selected_ssid) == 0) {
        nvs_set_blob(nvs, "bssid", selected_bssid, sizeof(selected_bssid));
        nvs_set_u8(nvs, "channel", selected_channel);
    } else {
        nvs_erase_key(nvs, "bssid");
        nvs_erase_key(nvs, "channel");
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool load_config(prg32_wifi_config_t *config) {
    nvs_handle_t nvs;
    if (!config || nvs_open("prg32wifi", NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    uint8_t mode = 0;
    size_t ssid_len = sizeof(config->ssid);
    size_t password_len = sizeof(config->password);
    size_t ap_ssid_len = sizeof(config->ap_ssid);
    size_t ap_password_len = sizeof(config->ap_password);
    uint8_t stored_bssid[6];
    uint8_t stored_channel = 0;
    size_t stored_bssid_len = sizeof(stored_bssid);
    esp_err_t err = nvs_get_u8(nvs, "mode", &mode);
    err |= nvs_get_str(nvs, "ssid", config->ssid, &ssid_len);
    err |= nvs_get_str(nvs, "password", config->password, &password_len);
    err |= nvs_get_str(nvs, "ap_ssid", config->ap_ssid, &ap_ssid_len);
    err |= nvs_get_str(nvs, "ap_password", config->ap_password, &ap_password_len);
    esp_err_t bssid_err = nvs_get_blob(nvs, "bssid", stored_bssid, &stored_bssid_len);
    esp_err_t channel_err = nvs_get_u8(nvs, "channel", &stored_channel);
    nvs_close(nvs);
    if (err != ESP_OK) {
        return false;
    }
    config->mode = (prg32_wifi_mode_t)mode;
    if (config->mode == PRG32_WIFI_MODE_APSTA) {
        config->mode = PRG32_WIFI_MODE_STA;
    }
    selected_ap_valid = false;
    selected_ap_locked = false;
    selected_ssid[0] = '\0';
    selected_channel = 0;
    if (config->mode == PRG32_WIFI_MODE_STA &&
        bssid_err == ESP_OK &&
        channel_err == ESP_OK &&
        stored_bssid_len == sizeof(stored_bssid) &&
        stored_channel > 0) {
        copy_cstr(selected_ssid, sizeof(selected_ssid), config->ssid);
        memcpy(selected_bssid, stored_bssid, sizeof(selected_bssid));
        selected_channel = stored_channel;
        selected_ap_valid = true;
        selected_ap_locked = false;
    }
    return true;
}

static int start_mode_locked(const prg32_wifi_config_t *config) {
    if (!config || config->mode == PRG32_WIFI_MODE_OFF) {
        active_mode = PRG32_WIFI_MODE_OFF;
        copy_cstr(active_status, sizeof(active_status), "OFF");
        return 0;
    }
    if (wifi_started) {
        esp_wifi_stop();
        wifi_started = false;
        active_mode = PRG32_WIFI_MODE_OFF;
        copy_cstr(active_status, sizeof(active_status), "RESTARTING");
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    if (wifi_stack_init_locked() != ESP_OK) {
        return -1;
    }

    wifi_config_t sta = {
        .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL
        }
    };
    copy_cstr((char *)sta.sta.ssid, sizeof(sta.sta.ssid), config->ssid);
    copy_cstr((char *)sta.sta.password,
              sizeof(sta.sta.password),
              config->password);
    if (selected_ap_valid &&
        strcmp(config->ssid, selected_ssid) == 0) {
        memcpy(sta.sta.bssid, selected_bssid, sizeof(sta.sta.bssid));
        sta.sta.bssid_set = true;
        sta.sta.channel = selected_channel;
    }
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.required = false;

    wifi_config_t ap = {0};
    copy_cstr((char *)ap.ap.ssid, sizeof(ap.ap.ssid), config->ap_ssid);
    copy_cstr((char *)ap.ap.password,
              sizeof(ap.ap.password),
              config->ap_password);
    ap.ap.ssid_len = strlen((const char *)ap.ap.ssid);
    ap.ap.channel = PRG32_WIFI_AP_CHANNEL;
    ap.ap.max_connection = PRG32_WIFI_AP_MAX_CONN;
    ap.ap.authmode = strlen((const char *)ap.ap.password) >= 8
        ? WIFI_AUTH_WPA_WPA2_PSK
        : WIFI_AUTH_OPEN;

    wifi_mode_t esp_mode = WIFI_MODE_NULL;
    if (config->mode == PRG32_WIFI_MODE_STA) {
        esp_mode = WIFI_MODE_STA;
    } else if (config->mode == PRG32_WIFI_MODE_AP) {
        esp_mode = WIFI_MODE_AP;
    } else {
        esp_mode = WIFI_MODE_APSTA;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(esp_mode));
    if (config->mode == PRG32_WIFI_MODE_STA ||
        config->mode == PRG32_WIFI_MODE_APSTA) {
        configure_sta_protocols();
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_LOGI(TAG,
                 "STA connecting: ssid=%s password_len=%u channel=%u bssid_set=%d",
                 config->ssid,
                 (unsigned)strlen(config->password),
                 (unsigned)sta.sta.channel,
                 sta.sta.bssid_set ? 1 : 0);
    }
    if (config->mode == PRG32_WIFI_MODE_AP ||
        config->mode == PRG32_WIFI_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    active_mode = config->mode;
    wifi_started = true;
    if (config->mode == PRG32_WIFI_MODE_AP ||
        config->mode == PRG32_WIFI_MODE_APSTA) {
        copy_cstr(active_ssid, sizeof(active_ssid), config->ap_ssid);
        copy_cstr(active_ip, sizeof(active_ip), "192.168.4.1");
        copy_cstr(active_status, sizeof(active_status), "READY");
    } else {
        copy_cstr(active_ssid, sizeof(active_ssid), config->ssid);
        copy_cstr(active_ip, sizeof(active_ip), "connecting");
        copy_cstr(active_status, sizeof(active_status), "CONNECTING");
    }

    if (config->mode == PRG32_WIFI_MODE_AP ||
        config->mode == PRG32_WIFI_MODE_APSTA) {
        ESP_LOGI(TAG,
                 "AP started: ssid=%s password=%s url=http://192.168.4.1",
                 config->ap_ssid,
                 config->ap_password);
    }
    return 0;
}

/* Bring back the mode saved in NVS after a scan stopped the radio. */
static void start_saved_mode_locked(void) {
    prg32_wifi_config_t config;
    default_config(&config);
    load_config(&config);
    start_mode_locked(&config);
}

int prg32_wifi_start_mode(const prg32_wifi_config_t *config) {
    wifi_lock_take();
    int rc = start_mode_locked(config);
    wifi_lock_give();
    return rc;
}

const char *prg32_wifi_current_status(void) {
    return active_status;
}

prg32_wifi_mode_t prg32_wifi_current_mode(void) {
    return active_mode;
}

const char *prg32_wifi_current_ip(void) {
    return active_ip;
}

const char *prg32_wifi_current_ssid(void) {
    return active_ssid;
}

int prg32_wifi_setup_requested(void) {
    if (PRG32_PIN_SETUP < 0) {
        return 0;
    }
    return gpio_get_level(PRG32_PIN_SETUP) == 0;
}

static const char *mode_name(prg32_wifi_mode_t mode) {
    if (mode == PRG32_WIFI_MODE_STA) {
        return "INFRASTRUCTURE";
    }
    if (mode == PRG32_WIFI_MODE_AP) {
        return "ACCESS POINT";
    }
    if (mode == PRG32_WIFI_MODE_APSTA) {
        return "AP+INFRA";
    }
    return "OFF";
}

static void draw_status(const prg32_wifi_config_t *config) {
    prg32_gfx_text8(8, 112, "SELECTED:", PRG32_COLOR_CYAN, 0);
    prg32_gfx_text8(88,
                     112,
                     config ? mode_name(config->mode) : "-",
                     PRG32_COLOR_CYAN,
                     0);
    prg32_gfx_text8(8, 128, "ACTIVE:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(72, 128, mode_name(active_mode), PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 144, "IP:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(40, 144, active_ip, PRG32_COLOR_GREEN, 0);
    if (active_mode == PRG32_WIFI_MODE_AP ||
        active_mode == PRG32_WIFI_MODE_APSTA) {
        prg32_gfx_text8(8, 160, "AP SSID:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(80, 160, active_ssid, PRG32_COLOR_GREEN, 0);
    } else if (active_mode == PRG32_WIFI_MODE_STA && active_ssid[0]) {
        prg32_gfx_text8(8, 160, "SSID:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(56, 160, active_ssid, PRG32_COLOR_GREEN, 0);
    }
    prg32_gfx_text8(8, 176, "STATUS:", PRG32_COLOR_YELLOW, 0);
    prg32_gfx_text8(72, 176, active_status, PRG32_COLOR_YELLOW, 0);
}

/* Returns 0 for access point, 1 for infrastructure, 2 to forget a stored
 * network, or -1 to leave. */
static int choose_mode(prg32_wifi_config_t *config) {
    int choice = config && config->mode == PRG32_WIFI_MODE_STA ? 1 : 0;
    uint32_t last = 0;
    prg32_input_wait_released(PRG32_BTN_UP |
                              PRG32_BTN_DOWN |
                              MENU_ACCEPT |
                              MENU_CANCEL);
    while (1) {
        if (prg32_setup_interrupted()) {
            return -1;
        }
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
            choice--;
        }
        if ((input & PRG32_BTN_DOWN) && !(last & PRG32_BTN_DOWN) && choice < 2) {
            choice++;
        }
        if ((input & MENU_ACCEPT) && !(last & MENU_ACCEPT)) {
            prg32_input_wait_released(MENU_ACCEPT);
            return choice;
        }
        if ((input & MENU_CANCEL) && !(last & MENU_CANCEL)) {
            prg32_input_wait_released(MENU_CANCEL);
            return -1;
        }
        if (config && choice < 2) {
            config->mode = choice == 0 ? PRG32_WIFI_MODE_AP : PRG32_WIFI_MODE_STA;
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "PRG32 WIFI SETUP", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8,
                         40,
                         choice == 0 ? "> ACCESS POINT" : "  ACCESS POINT",
                         PRG32_COLOR_WHITE,
                         0);
        prg32_gfx_text8(8,
                         56,
                         choice == 1 ? "> INFRASTRUCTURE" : "  INFRASTRUCTURE",
                         PRG32_COLOR_WHITE,
                         0);
        prg32_gfx_text8(8,
                         72,
                         choice == 2 ? "> FORGET NETWORK" : "  FORGET NETWORK",
                         PRG32_COLOR_WHITE,
                         0);
        draw_status(config);
        prg32_gfx_text8(8, 204, "UP/DOWN CHOOSE  SELECT/A OK  B BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static const wifi_scan_config_t PRG32_WIFI_SCAN_CONFIG = {
    .show_hidden = true,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .scan_time = {
        .active = {
            .min = 100,
            .max = 300
        }
    }
};

/* Run one blocking scan on a started station interface and return the
 * records (caller frees) or 0 with a reason in `status`. */
static int scan_records(wifi_ap_record_t **records_out,
                        char *status,
                        size_t status_size) {
    uint16_t found_count = 0;
    esp_err_t err = esp_wifi_scan_start(&PRG32_WIFI_SCAN_CONFIG, true);
    if (err == ESP_OK) {
        uint16_t count = 0;
        err = esp_wifi_scan_get_ap_num(&count);
        if (err == ESP_OK && count > 0) {
            wifi_ap_record_t *records = heap_caps_calloc(count,
                                                         sizeof(*records),
                                                         MALLOC_CAP_8BIT);
            if (!records) {
                err = ESP_ERR_NO_MEM;
            } else {
                err = esp_wifi_scan_get_ap_records(&count, records);
                if (err == ESP_OK) {
                    *records_out = records;
                    found_count = count;
                    records = NULL;
                }
                heap_caps_free(records);
            }
        }
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi scan found %u networks", (unsigned)found_count);
    } else {
        copy_cstr(status, status_size, esp_err_to_name(err));
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK ? (int)found_count : 0;
}

/* Scan from a stopped radio: Wi-Fi is restarted in station mode for the scan
 * and left stopped afterwards. */
static int scan_networks_locked(wifi_ap_record_t **records_out,
                                char *status,
                                size_t status_size) {
    if (!records_out || wifi_stack_init_locked() != ESP_OK) {
        copy_cstr(status, status_size, "WIFI INIT FAILED");
        return 0;
    }
    *records_out = NULL;
    /* Before the stop: stopping a connected station raises a disconnect event,
     * and the reconnect it would start ends the scan with nothing found. */
    sta_autoconnect = false;
    if (wifi_started) {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi stop before scan failed: %s",
                     esp_err_to_name(stop_err));
        }
        wifi_started = false;
        active_mode = PRG32_WIFI_MODE_OFF;
        copy_cstr(active_ip, sizeof(active_ip), "-");
        copy_cstr(active_status, sizeof(active_status), "OFF");
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        copy_cstr(status, status_size, esp_err_to_name(err));
        ESP_LOGE(TAG, "Wi-Fi scan set mode failed: %s", esp_err_to_name(err));
        sta_autoconnect = true;
        return 0;
    }
    configure_sta_protocols();

    err = esp_wifi_start();
    if (err != ESP_OK) {
        copy_cstr(status, status_size, esp_err_to_name(err));
        ESP_LOGE(TAG, "Wi-Fi scan start failed: %s", esp_err_to_name(err));
        sta_autoconnect = true;
        return 0;
    }
    wifi_started = true;
    vTaskDelay(pdMS_TO_TICKS(250));

    int found_count = scan_records(records_out, status, status_size);
    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi stop after scan failed: %s",
                 esp_err_to_name(stop_err));
    }
    wifi_started = false;
    sta_autoconnect = true;
    active_mode = PRG32_WIFI_MODE_OFF;
    if (found_count <= 0 || !*records_out) {
        if (status && status_size > 0 && status[0] == '\0') {
            copy_cstr(status, status_size, "NO NETWORKS FOUND");
        }
        return 0;
    }
    return found_count;
}

static int scan_networks(wifi_ap_record_t **records_out,
                         char *status,
                         size_t status_size) {
    wifi_lock_take();
    int count = scan_networks_locked(records_out, status, status_size);
    wifi_lock_give();
    return count;
}

static int compare_ap_rssi_desc(const void *lhs, const void *rhs) {
    const wifi_ap_record_t *a = (const wifi_ap_record_t *)lhs;
    const wifi_ap_record_t *b = (const wifi_ap_record_t *)rhs;
    return (int)b->rssi - (int)a->rssi;
}

static void select_ap_record(const wifi_ap_record_t *record) {
    if (!record) {
        selected_ap_valid = false;
        selected_ap_locked = false;
        selected_ssid[0] = '\0';
        selected_channel = 0;
        return;
    }
    selected_authmode = record->authmode;
    copy_cstr(selected_ssid, sizeof(selected_ssid), (const char *)record->ssid);
    memcpy(selected_bssid, record->bssid, sizeof(selected_bssid));
    selected_channel = record->primary;
    selected_ap_valid = true;
    selected_ap_locked = true;
}

static void select_ap_record_for_boot(const wifi_ap_record_t *record) {
    select_ap_record(record);
    // explicitly allow using the locked BSSID from refresh scan
    selected_ap_locked = true;
}

static const wifi_ap_record_t *strongest_record(const wifi_ap_record_t *records,
                                                int count,
                                                const char *ssid) {
    const wifi_ap_record_t *best = NULL;
    for (int i = 0; i < count; ++i) {
        if (strcmp((const char *)records[i].ssid, ssid) != 0) {
            continue;
        }
        if (!best || records[i].rssi > best->rssi) {
            best = &records[i];
        }
    }
    return best;
}

/* Before a station boot, look for the saved network. When it is out of
 * range, switch to the most recently connected stored network that is in
 * range, so a console moved between home and lab joins whichever it finds. */
static void refresh_stored_sta_ap(prg32_wifi_config_t *config) {
    if (!config ||
        config->mode != PRG32_WIFI_MODE_STA ||
        (config->ssid[0] == '\0' && known_count == 0)) {
        return;
    }

    wifi_ap_record_t *records = NULL;
    char scan_status[32] = "";
    int count = scan_networks(&records, scan_status, sizeof(scan_status));
    if (count <= 0) {
        ESP_LOGW(TAG,
                 "stored SSID refresh scan failed for %s: %s",
                 config->ssid,
                 scan_status[0] ? scan_status : "not found");
        heap_caps_free(records);
        return;
    }

    const wifi_ap_record_t *best = config->ssid[0]
        ? strongest_record(records, count, config->ssid)
        : NULL;
    if (!best) {
        for (int k = 0; k < known_count && !best; ++k) {
            if (strcmp(known_networks[k].ssid, config->ssid) == 0) {
                continue;
            }
            best = strongest_record(records, count, known_networks[k].ssid);
            if (best) {
                ESP_LOGI(TAG,
                         "saved SSID %s not in range, using stored network %s",
                         config->ssid[0] ? config->ssid : "(none)",
                         known_networks[k].ssid);
                copy_cstr(config->ssid, sizeof(config->ssid),
                          known_networks[k].ssid);
                copy_cstr(config->password, sizeof(config->password),
                          known_networks[k].password);
            }
        }
        if (best) {
            select_ap_record_for_boot(best);
            save_config(config);
        }
    } else {
        select_ap_record_for_boot(best);
    }
    if (best) {
        ESP_LOGI(TAG,
                 "refreshed stored SSID %s channel=%u rssi=%d",
                 config->ssid,
                 (unsigned)selected_channel,
                 (int)best->rssi);
    } else {
        ESP_LOGW(TAG, "stored SSID %s not found in refresh scan", config->ssid);
    }
    heap_caps_free(records);
}

static bool wait_connected(uint32_t timeout_ms) {
    uint32_t start = prg32_ticks_ms();
    while (strcmp(active_status, "CONNECTED") != 0 &&
           prg32_ticks_ms() - start < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return strcmp(active_status, "CONNECTED") == 0;
}

/* Record the network the station is connected to, when it is the saved one.
 * Called before the stored list is read, so a join that finished after the
 * boot wait is still remembered. Caller holds the Wi-Fi lock. */
static void remember_active_locked(void) {
    if (active_mode != PRG32_WIFI_MODE_STA ||
        strcmp(active_status, "CONNECTED") != 0) {
        return;
    }
    /* Read only the saved credentials: load_config() also resets the
     * selected access point, which must not change behind a connection. */
    char ssid[32] = "";
    char password[64] = "";
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);
    nvs_handle_t nvs;
    if (nvs_open("prg32wifi", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    bool ok = nvs_get_str(nvs, "ssid", ssid, &ssid_len) == ESP_OK &&
              nvs_get_str(nvs, "password", password, &password_len) == ESP_OK;
    nvs_close(nvs);
    if (ok && strcmp(ssid, active_ssid) == 0) {
        known_remember(ssid, password);
    }
    memset(password, 0, sizeof(password));
}

/* Remember the station network once it has really connected. */
static void remember_if_connected(const prg32_wifi_config_t *config) {
    if (!config ||
        active_mode != PRG32_WIFI_MODE_STA ||
        strcmp(active_status, "CONNECTED") != 0 ||
        strcmp(active_ssid, config->ssid) != 0) {
        return;
    }
    wifi_lock_take();
    known_remember(config->ssid, config->password);
    wifi_lock_give();
}

static int confirm_sta_credentials(const prg32_wifi_config_t *config) {
    uint32_t last = 0;
    if (config && auth_mode_is_enterprise(selected_authmode)) {
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "CONFIRM WIFI", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 40, "ENTERPRISE WIFI IS NOT SUPPORTED", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, 64, "USE WPA/WPA2 PERSONAL", PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(8, 204, "B BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
        while ((prg32_input_read_menu() & MENU_CANCEL) == 0 &&
               !prg32_setup_interrupted()) {
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        prg32_input_wait_released(PRG32_BTN_B);
        return -1;
    }
    prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
    while (1) {
        if (prg32_setup_interrupted()) {
            return -1;
        }
        uint32_t input = prg32_input_read_menu();
        if ((input & MENU_ACCEPT) && !(last & MENU_ACCEPT)) {
            prg32_input_wait_released(MENU_ACCEPT);
            return 0;
        }
        if ((input & MENU_CANCEL) && !(last & MENU_CANCEL)) {
            prg32_input_wait_released(MENU_CANCEL);
            return -1;
        }

        char line[48];
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "CONFIRM WIFI", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 40, "SSID:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(56, 40, config ? config->ssid : "", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 64, "PASSWORD:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 82, config ? config->password : "", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 104, "SECURITY:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(88, 104, auth_mode_name(selected_authmode), PRG32_COLOR_WHITE, 0);
        snprintf(line,
                 sizeof(line),
                 "PASSWORD LENGTH: %u",
                 config ? (unsigned)strlen(config->password) : 0);
        prg32_gfx_text8(8, 128, line, PRG32_COLOR_CYAN, 0);
        if (auth_mode_is_enterprise(selected_authmode)) {
            prg32_gfx_text8(8, 152, "ENTERPRISE WIFI IS NOT SUPPORTED", PRG32_COLOR_YELLOW, 0);
        }
        prg32_gfx_text8(8, 204, "SELECT/A CONNECT  B EDIT", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static int choose_ssid(char *ssid, size_t ssid_size) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "SCANNING WIFI", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 28, "MODE:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(56, 28, "INFRASTRUCTURE", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 44, "IP:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(40, 44, active_ip, PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 72, "PLEASE WAIT...", PRG32_COLOR_CYAN, 0);
    prg32_gfx_present();

    wifi_ap_record_t *records = NULL;
    char scan_status[32] = "";
    int count = scan_networks(&records, scan_status, sizeof(scan_status));
    if (count > 1) {
        qsort(records, (size_t)count, sizeof(*records), compare_ap_rssi_desc);
    }
    if (count <= 0) {
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "INFRASTRUCTURE WIFI", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 28, "MODE:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(56, 28, "INFRASTRUCTURE", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 44, "IP:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(40, 44, active_ip, PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8,
                         72,
                         scan_status[0] ? scan_status : "NO NETWORKS FOUND",
                         PRG32_COLOR_YELLOW,
                         0);
        prg32_gfx_text8(8, 96, "B BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        while (1) {
            if ((prg32_input_read_menu() & PRG32_BTN_B) ||
                prg32_setup_interrupted()) {
                return -1;
            }
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }

    int choice = 0;
    uint32_t last = 0;
    prg32_input_wait_released(PRG32_BTN_UP |
                              PRG32_BTN_DOWN |
                              MENU_ACCEPT |
                              MENU_CANCEL);
    while (1) {
        if (prg32_setup_interrupted()) {
            heap_caps_free(records);
            return -1;
        }
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
            choice--;
        }
        if ((input & PRG32_BTN_DOWN) &&
            !(last & PRG32_BTN_DOWN) &&
            choice + 1 < count) {
            choice++;
        }
        if ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B)) {
            heap_caps_free(records);
            prg32_input_wait_released(PRG32_BTN_B);
            return -1;
        }
        if ((input & MENU_ACCEPT) && !(last & MENU_ACCEPT)) {
            copy_cstr(ssid, ssid_size, (const char *)records[choice].ssid);
            select_ap_record(&records[choice]);
            heap_caps_free(records);
            prg32_input_wait_released(MENU_ACCEPT);
            return 0;
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "CHOOSE WIFI", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 24, "MODE:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(56, 24, "INFRASTRUCTURE", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(168, 24, "IP:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(200, 24, active_ip, PRG32_COLOR_GREEN, 0);
        int first = 0;
        if (choice >= PRG32_WIFI_SCAN_VISIBLE) {
            first = choice - PRG32_WIFI_SCAN_VISIBLE + 1;
        }
        char page[24];
        snprintf(page, sizeof(page), "%d/%d", choice + 1, count);
        prg32_gfx_text8(264, 24, page, PRG32_COLOR_CYAN, 0);
        for (int row = 0; row < PRG32_WIFI_SCAN_VISIBLE; ++row) {
            int i = first + row;
            if (i >= count) {
                break;
            }
            int y = 48 + row * 15;
            const char *name = (const char *)records[i].ssid;
            char detail[24];
            if (!name || name[0] == '\0') {
                name = "(hidden)";
            }
            snprintf(detail,
                     sizeof(detail),
                     "C%u %d",
                     (unsigned)records[i].primary,
                     (int)records[i].rssi);
            prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
            prg32_gfx_text8(24,
                             y,
                             name,
                             PRG32_COLOR_WHITE,
                             0);
            prg32_gfx_text8(184,
                             y,
                             detail,
                             PRG32_COLOR_CYAN,
                             0);
            prg32_gfx_text8(256,
                             y,
                             auth_mode_short_name(records[i].authmode),
                             PRG32_COLOR_YELLOW,
                             0);
        }
        prg32_gfx_text8(8, 184, "UP/DOWN  SELECT/A OK  B BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static void show_wifi_message(const char *title, const char *line,
                              uint16_t color) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, title, PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 40, line, color, 0);
    prg32_gfx_text8(8, 204, "B BACK", PRG32_COLOR_CYAN, 0);
    prg32_gfx_present();
    prg32_input_wait_released(MENU_ACCEPT | MENU_CANCEL);
    while ((prg32_input_read_menu() & MENU_CANCEL) == 0 &&
           !prg32_setup_interrupted()) {
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    prg32_input_wait_released(MENU_CANCEL);
}

/* List the stored networks and erase the one the player confirms. */
static void forget_network_menu(void) {
    char ssids[PRG32_WIFI_KNOWN_MAX][32];
    int count = prg32_wifi_known_networks(ssids, PRG32_WIFI_KNOWN_MAX);
    if (count <= 0) {
        show_wifi_message("FORGET NETWORK", "NO SAVED NETWORKS",
                          PRG32_COLOR_YELLOW);
        return;
    }
    int choice = 0;
    bool confirming = false;
    uint32_t last = 0;
    prg32_input_wait_released(PRG32_BTN_UP | PRG32_BTN_DOWN | MENU_ACCEPT |
                              MENU_CANCEL);
    while (1) {
        if (prg32_setup_interrupted()) {
            return;
        }
        uint32_t input = prg32_input_read_menu();
        if (!confirming) {
            if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
                choice--;
            }
            if ((input & PRG32_BTN_DOWN) && !(last & PRG32_BTN_DOWN) &&
                choice + 1 < count) {
                choice++;
            }
        }
        if ((input & MENU_CANCEL) && !(last & MENU_CANCEL)) {
            prg32_input_wait_released(MENU_CANCEL);
            if (!confirming) {
                return;
            }
            confirming = false;
            input = 0;
        }
        if ((input & MENU_ACCEPT) && !(last & MENU_ACCEPT)) {
            prg32_input_wait_released(MENU_ACCEPT);
            input = 0;
            if (!confirming) {
                confirming = true;
            } else {
                int rc = prg32_wifi_forget_network(ssids[choice]);
                show_wifi_message("FORGET NETWORK",
                                  rc == 0 ? "NETWORK FORGOTTEN"
                                          : "NETWORK NOT FOUND",
                                  rc == 0 ? PRG32_COLOR_GREEN
                                          : PRG32_COLOR_YELLOW);
                return;
            }
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "FORGET NETWORK", PRG32_COLOR_WHITE, 0);
        if (confirming) {
            prg32_gfx_text8(8, 40, "FORGET THIS NETWORK?", PRG32_COLOR_YELLOW, 0);
            prg32_gfx_text8(8, 60, ssids[choice], PRG32_COLOR_WHITE, 0);
            prg32_gfx_text8(8, 84, "ITS PASSWORD IS ERASED", PRG32_COLOR_CYAN, 0);
            prg32_gfx_text8(8, 204, "SELECT/A FORGET  B CANCEL",
                             PRG32_COLOR_CYAN, 0);
        } else {
            prg32_gfx_text8(8, 24, "SAVED NETWORKS, NEWEST FIRST",
                             PRG32_COLOR_GREEN, 0);
            for (int i = 0; i < count; ++i) {
                int y = 48 + i * 16;
                prg32_gfx_text8(8, y, i == choice ? ">" : " ",
                                 PRG32_COLOR_GREEN, 0);
                prg32_gfx_text8(24, y, ssids[i], PRG32_COLOR_WHITE, 0);
            }
            prg32_gfx_text8(8, 204, "UP/DOWN  SELECT/A FORGET  B BACK",
                             PRG32_COLOR_CYAN, 0);
        }
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

int prg32_wifi_setup_run(void) {
    int was_fullscreen = prg32_gfx_fullscreen_enabled();
    prg32_gfx_set_fullscreen(1);
    prg32_wifi_config_t config;
    default_config(&config);
    prg32_wifi_config_t stored;
    default_config(&stored);
    if (wifi_stack_init() == ESP_OK && load_config(&stored)) {
        config = stored;
    }

    bool scan_stopped_radio = false;
    while (1) {
        /* The player left the network screens without connecting. The check
         * of wifi_started leaves alone a mode that WIFI_SET started meanwhile. */
        if (scan_stopped_radio) {
            scan_stopped_radio = false;
            wifi_lock_take();
            if (!wifi_started) {
                start_saved_mode_locked();
            }
            wifi_lock_give();
        }
        int mode = choose_mode(&config);
        if (mode < 0) {
            prg32_gfx_set_fullscreen(was_fullscreen);
            return -1;
        }
        if (mode == 2) {
            forget_network_menu();
            /* Forgetting may have changed the saved configuration. */
            default_config(&stored);
            if (wifi_stack_init() == ESP_OK && load_config(&stored)) {
                config = stored;
            }
            continue;
        }
        if (mode == 0) {
            config.mode = PRG32_WIFI_MODE_AP;
            if (prg32_text_input(config.ap_ssid,
                                 sizeof(config.ap_ssid),
                                 "AP SSID") < 0 ||
                prg32_text_input(config.ap_password,
                                 sizeof(config.ap_password),
                                 "AP PASSWORD") < 0) {
                continue;
            }
        } else {
            config.mode = PRG32_WIFI_MODE_STA;
            scan_stopped_radio = wifi_started;
            if (choose_ssid(config.ssid, sizeof(config.ssid)) != 0) {
                continue;
            }
            /* Offer the password already stored for this network. */
            wifi_lock_take();
            int known_index = known_find(config.ssid);
            if (known_index >= 0) {
                copy_cstr(config.password,
                          sizeof(config.password),
                          known_networks[known_index].password);
            } else if (strcmp(config.ssid, stored.ssid) == 0) {
                copy_cstr(config.password,
                          sizeof(config.password),
                          stored.password);
            } else {
                config.password[0] = '\0';
            }
            wifi_lock_give();
            if (prg32_text_input(config.password,
                                 sizeof(config.password),
                                 "WIFI PASSWORD") < 0) {
                continue;
            }
            if (confirm_sta_credentials(&config) != 0) {
                continue;
            }
        }

        if (wifi_stack_init() == ESP_OK) {
            save_config(&config);
        }
        int rc = prg32_wifi_start_mode(&config);
        uint32_t start = prg32_ticks_ms();
        while (1) {
            prg32_gfx_clear(PRG32_COLOR_BLACK);
            prg32_gfx_text8(8, 8, "WIFI MODE", PRG32_COLOR_WHITE, 0);
            draw_status(&config);
            prg32_gfx_text8(8,
                             204,
                             rc == 0 ? "B BACK" : "WIFI FAILED",
                             PRG32_COLOR_CYAN,
                             0);
            prg32_gfx_present();
            if (rc != 0 ||
                strcmp(active_status, "CONNECTED") == 0 ||
                (prg32_input_read_menu() & PRG32_BTN_B) ||
                prg32_setup_interrupted()) {
                break;
            }
            if (prg32_ticks_ms() - start > 30000) {
                if (strcmp(active_status, "CONNECTING") == 0) {
                    copy_cstr(active_status,
                              sizeof(active_status),
                              "CONNECT TIMEOUT");
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        remember_if_connected(&config);
        prg32_gfx_set_fullscreen(was_fullscreen);
        return rc;
    }
    prg32_gfx_set_fullscreen(was_fullscreen);
    return -1;
}

void prg32_wifi_scores_init(void) {
    if (wifi_started && active_mode == PRG32_WIFI_MODE_STA) {
        return;
    }

    prg32_wifi_config_t config;
    default_config(&config);
    if (wifi_stack_init() == ESP_OK) {
        prg32_wifi_config_t stored;
        default_config(&stored);
        if (load_config(&stored)) {
            config = stored;
        }
    }
    refresh_stored_sta_ap(&config);
    prg32_wifi_start_mode(&config);
    if (config.mode == PRG32_WIFI_MODE_STA) {
        if (!wait_connected(10000)) {
            ESP_LOGW(TAG, "STA boot connect timed out, retrying SSID-only");
            selected_ap_locked = false;
            selected_ap_valid = false;
            prg32_wifi_start_mode(&config);
            wait_connected(10000);
        }
        remember_if_connected(&config);
    }
}

static int compare_network_rssi_desc(const void *lhs, const void *rhs) {
    const prg32_wifi_network_t *a = (const prg32_wifi_network_t *)lhs;
    const prg32_wifi_network_t *b = (const prg32_wifi_network_t *)rhs;
    return (int)b->rssi - (int)a->rssi;
}

int prg32_wifi_scan_networks(prg32_wifi_network_t *out, int max, char *status,
                             size_t status_size) {
    if (status && status_size > 0) {
        status[0] = '\0';
    }
    if (!out || max <= 0) {
        copy_cstr(status, status_size, "invalid scan buffer");
        return -1;
    }
    wifi_lock_take();
    if (wifi_stack_init_locked() != ESP_OK) {
        wifi_lock_give();
        copy_cstr(status, status_size, "Wi-Fi init failed");
        return -1;
    }
    wifi_ap_record_t *records = NULL;
    int count;
    if (wifi_started && active_mode == PRG32_WIFI_MODE_STA &&
        strcmp(active_status, "CONNECTED") == 0) {
        /* A connected station scans without dropping its connection. */
        count = scan_records(&records, status, status_size);
    } else {
        /* Otherwise the radio is restarted for the scan, then the saved
         * mode (access point or a station still connecting) comes back. */
        bool was_started = wifi_started;
        count = scan_networks_locked(&records, status, status_size);
        if (was_started) {
            start_saved_mode_locked();
        }
    }
    wifi_lock_give();
    if (count <= 0) {
        heap_caps_free(records);
        if (status && status[0] == '\0') {
            copy_cstr(status, status_size, "no networks found");
        }
        return count < 0 ? -1 : 0;
    }

    /* One entry per SSID, the strongest access point; hidden ones skipped. */
    int used = 0;
    for (int i = 0; i < count; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        int slot = -1;
        for (int j = 0; j < used; ++j) {
            if (strcmp(out[j].ssid, ssid) == 0) {
                slot = j;
                break;
            }
        }
        if (slot >= 0) {
            if (records[i].rssi > out[slot].rssi) {
                out[slot].rssi = records[i].rssi;
                out[slot].channel = records[i].primary;
                out[slot].auth = auth_mode_short_name(records[i].authmode);
            }
            continue;
        }
        if (used == max) {
            /* Full: replace the weakest entry when this one is stronger. */
            int weakest = 0;
            for (int j = 1; j < used; ++j) {
                if (out[j].rssi < out[weakest].rssi) {
                    weakest = j;
                }
            }
            if (records[i].rssi <= out[weakest].rssi) {
                continue;
            }
            slot = weakest;
        } else {
            slot = used++;
        }
        copy_cstr(out[slot].ssid, sizeof(out[slot].ssid), ssid);
        out[slot].rssi = records[i].rssi;
        out[slot].channel = records[i].primary;
        out[slot].auth = auth_mode_short_name(records[i].authmode);
    }
    heap_caps_free(records);
    qsort(out, (size_t)used, sizeof(*out), compare_network_rssi_desc);
    return used;
}

int prg32_wifi_known_networks(char (*ssids)[32], int max) {
    if (!ssids || max <= 0 || wifi_stack_init() != ESP_OK) {
        return 0;
    }
    wifi_lock_take();
    remember_active_locked();
    int count = known_count < max ? known_count : max;
    for (int i = 0; i < count; ++i) {
        copy_cstr(ssids[i], sizeof(ssids[i]), known_networks[i].ssid);
    }
    wifi_lock_give();
    return count;
}

int prg32_wifi_connect_network(const char *ssid, const char *password,
                               uint32_t timeout_ms, char *status,
                               size_t status_size) {
    size_t ssid_len = ssid ? strlen(ssid) : 0;
    size_t password_len = password ? strlen(password) : 0;
    if (ssid_len == 0 || ssid_len >= sizeof(((prg32_wifi_config_t *)0)->ssid)) {
        copy_cstr(status, status_size, "SSID must be 1 to 31 characters");
        return -1;
    }
    if (password_len >= sizeof(((prg32_wifi_config_t *)0)->password) ||
        (password_len > 0 && password_len < 8)) {
        copy_cstr(status, status_size,
                  "password must be empty or 8 to 63 characters");
        return -1;
    }
    wifi_lock_take();
    if (wifi_stack_init_locked() != ESP_OK) {
        wifi_lock_give();
        copy_cstr(status, status_size, "Wi-Fi init failed");
        return -1;
    }
    prg32_wifi_config_t previous;
    default_config(&previous);
    load_config(&previous);
    bool was_started = wifi_started;

    prg32_wifi_config_t config = previous;
    config.mode = PRG32_WIFI_MODE_STA;
    copy_cstr(config.ssid, sizeof(config.ssid), ssid);
    copy_cstr(config.password, sizeof(config.password), password);
    select_ap_record(NULL); /* no BSSID pin: join any access point of it */
    int rc = start_mode_locked(&config);
    uint32_t start = prg32_ticks_ms();
    while (rc == 0 && strcmp(active_status, "CONNECTED") != 0 &&
           prg32_ticks_ms() - start < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (rc == 0 && strcmp(active_status, "CONNECTED") == 0) {
        save_config(&config);
        known_remember(config.ssid, config.password);
        copy_cstr(status, status_size, "connected");
        wifi_lock_give();
        return 0;
    }

    copy_cstr(status, status_size,
              strcmp(active_status, "CONNECTING") == 0 ? "connect timeout"
                                                       : active_status);
    ESP_LOGW(TAG, "join %s failed (%s); restoring previous Wi-Fi", ssid,
             status ? status : "");
    if (was_started) {
        start_mode_locked(&previous);
    } else {
        esp_wifi_stop();
        wifi_started = false;
        active_mode = PRG32_WIFI_MODE_OFF;
    }
    wifi_lock_give();
    return -1;
}

int prg32_wifi_forget_network(const char *ssid) {
    if (!ssid || ssid[0] == '\0') {
        return -1;
    }
    wifi_lock_take();
    if (wifi_stack_init_locked() != ESP_OK) {
        wifi_lock_give();
        return -1;
    }
    remember_active_locked();
    prg32_wifi_config_t config;
    default_config(&config);
    bool have_config = load_config(&config);
    int index = known_find(ssid);
    bool is_current = have_config && config.mode == PRG32_WIFI_MODE_STA &&
                      strcmp(config.ssid, ssid) == 0;
    if (index < 0 && !is_current) {
        wifi_lock_give();
        return -1;
    }
    known_remove(index);
    if (is_current) {
        memset(config.ssid, 0, sizeof(config.ssid));
        memset(config.password, 0, sizeof(config.password));
        select_ap_record(NULL);
        if (known_count > 0) {
            config.mode = PRG32_WIFI_MODE_STA;
            copy_cstr(config.ssid, sizeof(config.ssid), known_networks[0].ssid);
            copy_cstr(config.password, sizeof(config.password),
                      known_networks[0].password);
        } else {
            /* Nothing left to join: stay reachable as an access point. */
            config.mode = PRG32_WIFI_MODE_AP;
        }
        save_config(&config);
    }
    /* The Wi-Fi driver keeps its own copy of the last credentials and a cache
     * of recent access points, passwords included, in its NVS namespace.
     * Reset it so the forgotten password is erased there too, then restart
     * the remaining configuration. The next join may need one extra attempt
     * because the driver's cache starts empty. */
    bool restart = wifi_started;
    if (wifi_started) {
        esp_wifi_stop();
        wifi_started = false;
        active_mode = PRG32_WIFI_MODE_OFF;
    }
    esp_wifi_restore();
    if (restart) {
        start_mode_locked(&config);
    }
    ESP_LOGI(TAG, "forgot network %s (%d stored)", ssid, known_count);
    wifi_lock_give();
    return 0;
}

#else

#include <stdio.h>

void prg32_wifi_scores_init(void) {}
int prg32_wifi_start_mode(const prg32_wifi_config_t *config) {
    (void)config;
    return 0;
}
prg32_wifi_mode_t prg32_wifi_current_mode(void) {
    return PRG32_WIFI_MODE_OFF;
}
const char *prg32_wifi_current_ip(void) {
    return "-";
}
const char *prg32_wifi_current_ssid(void) {
    return "";
}
int prg32_wifi_setup_requested(void) {
    return 0;
}
int prg32_wifi_setup_run(void) {
    return 0;
}
int prg32_wifi_scan_networks(prg32_wifi_network_t *out, int max, char *status,
                             size_t status_size) {
    (void)out;
    (void)max;
    if (status && status_size > 0) {
        snprintf(status, status_size, "Wi-Fi is not available in this build");
    }
    return -1;
}
int prg32_wifi_known_networks(char (*ssids)[32], int max) {
    (void)ssids;
    (void)max;
    return 0;
}
int prg32_wifi_connect_network(const char *ssid, const char *password,
                               uint32_t timeout_ms, char *status,
                               size_t status_size) {
    (void)ssid;
    (void)password;
    (void)timeout_ms;
    if (status && status_size > 0) {
        snprintf(status, status_size, "Wi-Fi is not available in this build");
    }
    return -1;
}
int prg32_wifi_forget_network(const char *ssid) {
    (void)ssid;
    return -1;
}
const char *prg32_wifi_current_status(void) {
    return "OFF";
}

#endif
