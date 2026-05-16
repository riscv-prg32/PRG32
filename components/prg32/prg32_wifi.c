#include "prg32.h"
#include "prg32_config.h"

#if PRG32_WIFI_ENABLE

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "prg32_wifi";
static bool wifi_started;
static bool wifi_initialized;
static prg32_wifi_mode_t active_mode = PRG32_WIFI_MODE_OFF;

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

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi connected");
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

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
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

static void default_config(prg32_wifi_config_t *config) {
    memset(config, 0, sizeof(*config));
#if PRG32_WIFI_STA_ENABLE && PRG32_WIFI_AP_ENABLE
    config->mode = PRG32_WIFI_MODE_APSTA;
#elif PRG32_WIFI_STA_ENABLE
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
    esp_err_t err = nvs_get_u8(nvs, "mode", &mode);
    err |= nvs_get_str(nvs, "ssid", config->ssid, &ssid_len);
    err |= nvs_get_str(nvs, "password", config->password, &password_len);
    err |= nvs_get_str(nvs, "ap_ssid", config->ap_ssid, &ap_ssid_len);
    err |= nvs_get_str(nvs, "ap_password", config->ap_password, &ap_password_len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        return false;
    }
    config->mode = (prg32_wifi_mode_t)mode;
    return true;
}

int prg32_wifi_start_mode(const prg32_wifi_config_t *config) {
    if (!config || config->mode == PRG32_WIFI_MODE_OFF) {
        active_mode = PRG32_WIFI_MODE_OFF;
        return 0;
    }
    if (wifi_started) {
        return 0;
    }
    if (wifi_stack_init() != ESP_OK) {
        return -1;
    }

    wifi_config_t sta = {0};
    copy_cstr((char *)sta.sta.ssid, sizeof(sta.sta.ssid), config->ssid);
    copy_cstr((char *)sta.sta.password,
              sizeof(sta.sta.password),
              config->password);

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
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
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

int prg32_wifi_setup_requested(void) {
    if (PRG32_PIN_SETUP < 0) {
        return 0;
    }
    return gpio_get_level(PRG32_PIN_SETUP) == 0;
}

static int choose_mode(void) {
    int choice = 0;
    uint32_t last = 0;
    while (1) {
        uint32_t input = prg32_input_read();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP)) {
            choice = 0;
        }
        if ((input & PRG32_BTN_DOWN) && !(last & PRG32_BTN_DOWN)) {
            choice = 1;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            return choice;
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
                         choice == 1 ? "> CONNECT WIFI" : "  CONNECT WIFI",
                         PRG32_COLOR_WHITE,
                         0);
        prg32_gfx_text8(8, 88, "UP/DOWN CHOOSE  A OK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

int prg32_wifi_setup_run(void) {
    prg32_wifi_config_t config;
    default_config(&config);

    int mode = choose_mode();
    if (mode == 0) {
        config.mode = PRG32_WIFI_MODE_AP;
        prg32_text_input(config.ap_ssid, sizeof(config.ap_ssid), "AP SSID");
        prg32_text_input(config.ap_password,
                         sizeof(config.ap_password),
                         "AP PASSWORD");
    } else {
        config.mode = PRG32_WIFI_MODE_STA;
        prg32_text_input(config.ssid, sizeof(config.ssid), "WIFI SSID");
        prg32_text_input(config.password,
                         sizeof(config.password),
                         "WIFI PASSWORD");
    }

    if (wifi_stack_init() == ESP_OK) {
        save_config(&config);
    }
    return prg32_wifi_start_mode(&config);
}

void prg32_wifi_scores_init(void) {
    if (wifi_started) {
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
    prg32_wifi_start_mode(&config);
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
int prg32_wifi_setup_requested(void) {
    return 0;
}
int prg32_wifi_setup_run(void) {
    return 0;
}

#endif
