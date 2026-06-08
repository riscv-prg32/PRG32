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
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
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
    case WIFI_AUTH_WPA3_ENTERPRISE:
        return "WPA3-ENT";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        return "ENT";
    default:
        return "SECURE";
    }
}

static bool auth_mode_is_enterprise(wifi_auth_mode_t mode) {
    return mode == WIFI_AUTH_WPA2_ENTERPRISE ||
           mode == WIFI_AUTH_WPA3_ENTERPRISE ||
           mode == WIFI_AUTH_WPA2_WPA3_ENTERPRISE;
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
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        return "ENT";
    default:
        return "SEC";
    }
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
        esp_wifi_connect();
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(active_ip, sizeof(active_ip), IPSTR, IP2STR(&event->ip_info.ip));
        copy_cstr(active_status, sizeof(active_status), "CONNECTED");
        ESP_LOGI(TAG, "Wi-Fi connected ip=%s", active_ip);
    }
}

static esp_err_t wifi_stack_init(void) {
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
    wifi_initialized = true;
    return ESP_OK;
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

int prg32_wifi_start_mode(const prg32_wifi_config_t *config) {
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
    if (wifi_stack_init() != ESP_OK) {
        return -1;
    }

    wifi_config_t sta = {0};
    copy_cstr((char *)sta.sta.ssid, sizeof(sta.sta.ssid), config->ssid);
    copy_cstr((char *)sta.sta.password,
              sizeof(sta.sta.password),
              config->password);
    if (selected_ap_valid &&
        selected_ap_locked &&
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

static int choose_mode(prg32_wifi_config_t *config) {
    int choice = config && config->mode == PRG32_WIFI_MODE_STA ? 1 : 0;
    uint32_t last = 0;
    prg32_input_wait_released(PRG32_BTN_UP |
                              PRG32_BTN_DOWN |
                              PRG32_BTN_A |
                              PRG32_BTN_B |
                              PRG32_BTN_SELECT);
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP)) {
            choice = 0;
        }
        if ((input & PRG32_BTN_DOWN) && !(last & PRG32_BTN_DOWN)) {
            choice = 1;
        }
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            prg32_input_wait_released(PRG32_BTN_B | PRG32_BTN_SELECT);
            return choice;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_input_wait_released(PRG32_BTN_A);
            return -1;
        }
        if (config) {
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
        draw_status(config);
        prg32_gfx_text8(8, 204, "UP/DOWN CHOOSE  SELECT/B OK  A BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static int scan_networks(wifi_ap_record_t **records_out,
                         char *status,
                         size_t status_size) {
    if (!records_out || wifi_stack_init() != ESP_OK) {
        copy_cstr(status, status_size, "WIFI INIT FAILED");
        return 0;
    }
    *records_out = NULL;
    if (wifi_started) {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi stop before scan failed: %s",
                     esp_err_to_name(stop_err));
        }
        wifi_started = false;
        active_mode = PRG32_WIFI_MODE_OFF;
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    sta_autoconnect = false;
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

    wifi_scan_config_t scan = {
        .show_hidden = true,
    };
    uint16_t found_count = 0;
    err = esp_wifi_scan_start(&scan, true);
    if (err == ESP_OK) {
        uint16_t count = 0;
        err = esp_wifi_scan_get_ap_num(&count);
        if (err == ESP_OK && count > 0) {
            wifi_ap_record_t *records = calloc(count, sizeof(*records));
            if (!records) {
                err = ESP_ERR_NO_MEM;
            } else {
                err = esp_wifi_scan_get_ap_records(&count, records);
                if (err == ESP_OK) {
                    *records_out = records;
                    found_count = count;
                    records = NULL;
                }
                free(records);
            }
        }
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi scan found %u networks", (unsigned)found_count);
    } else {
        copy_cstr(status, status_size, esp_err_to_name(err));
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
    }
    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi stop after scan failed: %s",
                 esp_err_to_name(stop_err));
    }
    wifi_started = false;
    sta_autoconnect = true;
    active_mode = PRG32_WIFI_MODE_OFF;
    if (err != ESP_OK || !*records_out) {
        if (status && status_size > 0 && status[0] == '\0') {
            copy_cstr(status, status_size, "NO NETWORKS FOUND");
        }
        return 0;
    }
    return (int)found_count;
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
    selected_ap_locked = false;
}

static void refresh_stored_sta_ap(const prg32_wifi_config_t *config) {
    if (!config ||
        config->mode != PRG32_WIFI_MODE_STA ||
        config->ssid[0] == '\0') {
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
        free(records);
        return;
    }

    const wifi_ap_record_t *best = NULL;
    for (int i = 0; i < count; ++i) {
        if (strcmp((const char *)records[i].ssid, config->ssid) != 0) {
            continue;
        }
        if (!best || records[i].rssi > best->rssi) {
            best = &records[i];
        }
    }
    if (best) {
        select_ap_record_for_boot(best);
        ESP_LOGI(TAG,
                 "refreshed stored SSID %s channel=%u rssi=%d",
                 config->ssid,
                 (unsigned)selected_channel,
                 (int)best->rssi);
    } else {
        ESP_LOGW(TAG, "stored SSID %s not found in refresh scan", config->ssid);
    }
    free(records);
}

static int confirm_sta_credentials(const prg32_wifi_config_t *config) {
    uint32_t last = 0;
    if (config && auth_mode_is_enterprise(selected_authmode)) {
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "CONFIRM WIFI", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 40, "ENTERPRISE WIFI IS NOT SUPPORTED", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, 64, "USE WPA/WPA2 PERSONAL", PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(8, 204, "A BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT);
        while ((prg32_input_read_menu() & PRG32_BTN_A) == 0) {
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        prg32_input_wait_released(PRG32_BTN_A);
        return -1;
    }
    prg32_input_wait_released(PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT);
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            prg32_input_wait_released(PRG32_BTN_B | PRG32_BTN_SELECT);
            return 0;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_input_wait_released(PRG32_BTN_A);
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
        prg32_gfx_text8(8, 204, "SELECT/B CONNECT  A EDIT", PRG32_COLOR_CYAN, 0);
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
        prg32_gfx_text8(8, 96, "A BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        while (1) {
            if (prg32_input_read_menu() & PRG32_BTN_A) {
                return -1;
            }
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }

    int choice = 0;
    uint32_t last = 0;
    prg32_input_wait_released(PRG32_BTN_UP |
                              PRG32_BTN_DOWN |
                              PRG32_BTN_A |
                              PRG32_BTN_B |
                              PRG32_BTN_SELECT);
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
            choice--;
        }
        if ((input & PRG32_BTN_DOWN) &&
            !(last & PRG32_BTN_DOWN) &&
            choice + 1 < count) {
            choice++;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            free(records);
            prg32_input_wait_released(PRG32_BTN_A);
            return -1;
        }
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            copy_cstr(ssid, ssid_size, (const char *)records[choice].ssid);
            select_ap_record(&records[choice]);
            free(records);
            prg32_input_wait_released(PRG32_BTN_B | PRG32_BTN_SELECT);
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
        prg32_gfx_text8(8, 184, "UP/DOWN  SELECT/B OK  A BACK", PRG32_COLOR_CYAN, 0);
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

    while (1) {
        int mode = choose_mode(&config);
        if (mode < 0) {
            prg32_gfx_set_fullscreen(was_fullscreen);
            return -1;
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
            if (choose_ssid(config.ssid, sizeof(config.ssid)) != 0) {
                continue;
            }
            if (strcmp(config.ssid, stored.ssid) == 0) {
                copy_cstr(config.password,
                          sizeof(config.password),
                          stored.password);
            } else {
                config.password[0] = '\0';
            }
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
                             rc == 0 ? "A BACK" : "WIFI FAILED",
                             PRG32_COLOR_CYAN,
                             0);
            prg32_gfx_present();
            if (rc != 0 ||
                strcmp(active_status, "CONNECTED") == 0 ||
                (prg32_input_read_menu() & PRG32_BTN_A)) {
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
        uint32_t start = prg32_ticks_ms();
        while (strcmp(active_status, "CONNECTED") != 0 &&
               prg32_ticks_ms() - start < 10000) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        if (strcmp(active_status, "CONNECTED") != 0) {
            ESP_LOGW(TAG, "STA boot connect timed out, retrying SSID-only");
            selected_ap_locked = false;
            selected_ap_valid = false;
            prg32_wifi_start_mode(&config);
        }
    }
}

#else

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

#endif
