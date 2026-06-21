#include "prg32.h"
#include "prg32_config.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void prg32_display_init(void);
void prg32_input_init(void);
void prg32_audio_pwm_init(void);
void prg32_abi_exports_keep(void);

#include "nvs_flash.h"

#ifndef PRG32_BOOT_SETUP_MODE
#define PRG32_BOOT_SETUP_MODE 0
#endif

#ifndef CONFIG_PRG32_AUDIO_ENABLED
#define CONFIG_PRG32_AUDIO_ENABLED 0
#endif

#ifndef CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO
#define CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO -1
#endif

#ifndef CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO
#define CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO -1
#endif

#ifndef CONFIG_PRG32_AUDIO_I2S_DATA_GPIO
#define CONFIG_PRG32_AUDIO_I2S_DATA_GPIO -1
#endif

#ifndef CONFIG_PRG32_AUDIO_I2S_SD_GPIO
#define CONFIG_PRG32_AUDIO_I2S_SD_GPIO -1
#endif

#define SETUP_ACCEPT (PRG32_BTN_SELECT | PRG32_BTN_B)
#define SETUP_CANCEL PRG32_BTN_A
#define SETUP_NAV (PRG32_BTN_UP | PRG32_BTN_DOWN)
#define SETUP_ADJUST (PRG32_BTN_LEFT | PRG32_BTN_RIGHT)
#define SETUP_KEYS (SETUP_ACCEPT | SETUP_CANCEL | SETUP_NAV | SETUP_ADJUST)

typedef enum {
    SETUP_OPTION_RUN_CART,
    SETUP_OPTION_DEFAULT_CART,
    SETUP_OPTION_WIFI,
    SETUP_OPTION_STORE_CONFIG,
    SETUP_OPTION_STORE_BROWSE,
    SETUP_OPTION_AUDIO,
    SETUP_OPTION_DEVELOPER,
    SETUP_OPTION_PERFORMANCE,
    SETUP_OPTION_ABOUT,
    SETUP_OPTION_EXIT,
} setup_option_id_t;

typedef struct {
    setup_option_id_t id;
    const char *label;
} setup_option_t;

static const char *setup_mode_name(prg32_wifi_mode_t mode) {
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

static void draw_setup_status(int y) {
    const char *ssid = prg32_wifi_current_ssid();
    prg32_wifi_mode_t mode = prg32_wifi_current_mode();
    prg32_gfx_text8(8, y, "MODE:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(56, y, setup_mode_name(mode), PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, y + 16, "IP:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(40, y + 16, prg32_wifi_current_ip(), PRG32_COLOR_GREEN, 0);
    if (ssid && ssid[0]) {
        prg32_gfx_text8(8, y + 32, "SSID:", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(56, y + 32, ssid, PRG32_COLOR_GREEN, 0);
    }
}

static unsigned long bytes_to_kib(size_t bytes) {
    return (unsigned long)((bytes + 1023u) / 1024u);
}

static size_t setup_available_cart_flash(void) {
    size_t available = 0;
    for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
        size_t slot_size = prg32_cart_slot_size(slot);
        prg32_cart_info_t info;
        if (slot_size == 0 ||
            prg32_cart_get_slot_info(slot, &info) != 0) {
            continue;
        }
        if (!info.stored) {
            available += slot_size;
            continue;
        }
        size_t used = (size_t)info.code_size + (size_t)info.audio_size;
        if (used < slot_size) {
            available += slot_size - used;
        }
    }
    return available;
}

static void draw_setup_resources(int y) {
    char line[48];
    snprintf(line,
             sizeof(line),
             "RAM: %luK  CART FLASH: %luK",
             bytes_to_kib(esp_get_free_heap_size()),
             bytes_to_kib(setup_available_cart_flash()));
    prg32_gfx_text8(8, y, line, PRG32_COLOR_YELLOW, 0);
    snprintf(line,
             sizeof(line),
             "CART RAM: %luK",
             bytes_to_kib(prg32_cart_ram_size()));
    prg32_gfx_text8(8, y + 8, line, PRG32_COLOR_YELLOW, 0);
}

static int stored_slots(uint8_t *slots, int max_slots) {
    int count = 0;
    for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
        prg32_cart_info_t info;
        if (prg32_cart_get_slot_info(slot, &info) == 0 && info.stored) {
            if (slots && count < max_slots) {
                slots[count] = slot;
            }
            count++;
        }
    }
    return count;
}

static void show_setup_message(const char *title,
                               const char *line,
                               uint16_t color,
                               uint32_t ms) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, title ? title : "PRG32 SETUP", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 40, line ? line : "", color, 0);
    draw_setup_status(96);
    prg32_gfx_present();
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void start_performance_http_api(void) {
    if (prg32_wifi_current_mode() != PRG32_WIFI_MODE_OFF) {
        prg32_scores_api_start();
        return;
    }

#if PRG32_WIFI_AP_ENABLE
    prg32_wifi_config_t config = {
        .mode = PRG32_WIFI_MODE_AP,
    };
    snprintf(config.ap_ssid, sizeof(config.ap_ssid), "%s", PRG32_WIFI_AP_SSID);
    snprintf(config.ap_password,
             sizeof(config.ap_password),
             "%s",
             PRG32_WIFI_AP_PASSWORD);
    prg32_wifi_start_mode(&config);
#else
    prg32_wifi_scores_init();
#endif
    prg32_scores_api_start();
}

static void draw_cartridge_status(int y) {
    uint8_t slots[PRG32_CART_SLOT_COUNT];
    int count = stored_slots(slots, PRG32_CART_SLOT_COUNT);
    char line[64];
    snprintf(line, sizeof(line), "CARTRIDGES: %d", count);
    prg32_gfx_text8(8, y, line, PRG32_COLOR_CYAN, 0);

    int default_slot = prg32_cart_default_slot();
    if (default_slot >= 0) {
        prg32_cart_info_t info;
        prg32_cart_get_slot_info((uint8_t)default_slot, &info);
        snprintf(line,
                 sizeof(line),
                 "DEFAULT: %s %.24s",
                 info.slot_name,
                 info.name[0] ? info.name : "(unnamed)");
    } else {
        snprintf(line, sizeof(line), "DEFAULT: NONE");
    }
    prg32_gfx_text8(8, y + 16, line, PRG32_COLOR_CYAN, 0);
}

typedef enum {
    CART_ACTION_NONE,
    CART_ACTION_RUN,
    CART_ACTION_REFRESH,
} cart_action_result_t;

static cart_action_result_t cartridge_context_menu(uint8_t slot) {
    static const char *const actions[] = {
        "RUN CARTRIDGE",
        "RESET LOCAL SCOREBOARD",
        "REMOVE FROM SLOT",
    };
    const int action_count = (int)(sizeof(actions) / sizeof(actions[0]));
    int choice = 0;
    prg32_cart_info_t info;
    prg32_cart_get_slot_info(slot, &info);

    prg32_input_wait_released(SETUP_KEYS);
    uint32_t last = 0;
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
            choice--;
        }
        if ((input & PRG32_BTN_DOWN) &&
            !(last & PRG32_BTN_DOWN) &&
            choice + 1 < action_count) {
            choice++;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_input_wait_released(SETUP_CANCEL);
            return CART_ACTION_NONE;
        }
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            prg32_input_wait_released(SETUP_ACCEPT);
            if (choice == 0) {
                return CART_ACTION_RUN;
            }
            if (choice == 1) {
                int removed = prg32_score_reset_local(info.name[0] ? info.name : NULL);
                char line[48];
                snprintf(line,
                         sizeof(line),
                         removed == 1 ? "1 SCORE REMOVED" : "%d SCORES REMOVED",
                         removed);
                show_setup_message("LOCAL SCOREBOARD",
                                   line,
                                   PRG32_COLOR_GREEN,
                                   1000);
                return CART_ACTION_NONE;
            }
            if (prg32_cart_erase_slot(slot) == 0) {
                show_setup_message("RUN CARTRIDGE",
                                   "CARTRIDGE REMOVED",
                                   PRG32_COLOR_GREEN,
                                   1000);
                return CART_ACTION_REFRESH;
            }
            show_setup_message("RUN CARTRIDGE",
                               prg32_cart_last_error(),
                               PRG32_COLOR_RED,
                               1400);
            return CART_ACTION_NONE;
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "CARTRIDGE MENU", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 28, info.slot_name, PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(72,
                        28,
                        info.name[0] ? info.name : "(unnamed)",
                        PRG32_COLOR_WHITE,
                        0);
        for (int i = 0; i < action_count; ++i) {
            int y = 64 + i * 22;
            prg32_gfx_text8(24, y, actions[i], PRG32_COLOR_WHITE, 0);
            prg32_gfx_text8(8,
                             y,
                             i == choice ? ">" : " ",
                             PRG32_COLOR_GREEN,
                             0);
        }
        prg32_gfx_text8(8, 216, "UP/DOWN CHOOSE  SELECT/B OK  A BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static int cartridge_picker(const char *title, bool force_choice) {
    uint8_t slots[PRG32_CART_SLOT_COUNT];
    int count = stored_slots(slots, PRG32_CART_SLOT_COUNT);
    if (count <= 0) {
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, title ? title : "CARTRIDGES", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 40, "NO CARTRIDGE AVAILABLE", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, 64, "UPLOAD A GAME OVER WIFI", PRG32_COLOR_CYAN, 0);
        draw_setup_status(104);
        prg32_gfx_present();
        vTaskDelay(pdMS_TO_TICKS(1200));
        return -1;
    }

    if (count == 1 && !force_choice) {
        return prg32_cart_select_slot(slots[0]);
    }

    int choice = 0;
    prg32_input_wait_released(SETUP_KEYS);
    uint32_t last = 0;
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
        if ((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) {
            cart_action_result_t action = cartridge_context_menu(slots[choice]);
            if (action == CART_ACTION_RUN) {
                return prg32_cart_select_slot(slots[choice]);
            }
            if (action == CART_ACTION_REFRESH) {
                count = stored_slots(slots, PRG32_CART_SLOT_COUNT);
                if (count <= 0) {
                    return -1;
                }
                if (choice >= count) {
                    choice = count - 1;
                }
            }
            last = 0;
            continue;
        }
        if ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B)) {
            prg32_input_wait_released(PRG32_BTN_B);
            return prg32_cart_select_slot(slots[choice]);
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_input_wait_released(SETUP_CANCEL);
            return -1;
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, title ? title : "SELECT CARTRIDGE", PRG32_COLOR_WHITE, 0);
        for (int i = 0; i < count; ++i) {
            prg32_cart_info_t info;
            prg32_cart_get_slot_info(slots[i], &info);
            int y = 36 + i * 18;
            prg32_gfx_text8(8,
                             y,
                             i == choice ? ">" : " ",
                             PRG32_COLOR_GREEN,
                             0);
            prg32_gfx_text8(24, y, info.slot_name, PRG32_COLOR_CYAN, 0);
            prg32_gfx_text8(80,
                             y,
                             info.name[0] ? info.name : "(unnamed)",
                             PRG32_COLOR_WHITE,
                             0);
        }
        draw_setup_status(144);
        prg32_gfx_text8(8, 216, "B RUN  SELECT MENU  A BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static int default_cartridge_picker(void) {
    uint8_t slots[PRG32_CART_SLOT_COUNT];
    int count = stored_slots(slots, PRG32_CART_SLOT_COUNT);
    if (count <= 0) {
        show_setup_message("DEFAULT CARTRIDGE",
                           "NO CARTRIDGE AVAILABLE",
                           PRG32_COLOR_YELLOW,
                           1000);
        return -1;
    }

    int default_slot = prg32_cart_default_slot();
    int choice = count;
    for (int i = 0; i < count; ++i) {
        if (slots[i] == default_slot) {
            choice = i;
        }
    }

    prg32_input_wait_released(SETUP_KEYS);
    uint32_t last = 0;
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
            choice--;
        }
        if ((input & PRG32_BTN_DOWN) &&
            !(last & PRG32_BTN_DOWN) &&
            choice < count) {
            choice++;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_input_wait_released(SETUP_CANCEL);
            return -1;
        }
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            prg32_input_wait_released(SETUP_ACCEPT);
            int rc = choice < count
                ? prg32_cart_set_default_slot(slots[choice])
                : prg32_cart_set_default_slot(-1);
            show_setup_message("DEFAULT CARTRIDGE",
                               rc == 0 ? "DEFAULT SAVED" : prg32_cart_last_error(),
                               rc == 0 ? PRG32_COLOR_GREEN : PRG32_COLOR_RED,
                               900);
            return rc;
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "DEFAULT CARTRIDGE", PRG32_COLOR_WHITE, 0);
        for (int i = 0; i < count; ++i) {
            prg32_cart_info_t info;
            prg32_cart_get_slot_info(slots[i], &info);
            int y = 40 + i * 18;
            prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
            prg32_gfx_text8(24, y, slots[i] == default_slot ? "*" : " ", PRG32_COLOR_YELLOW, 0);
            prg32_gfx_text8(40, y, info.slot_name, PRG32_COLOR_CYAN, 0);
            prg32_gfx_text8(96,
                             y,
                             info.name[0] ? info.name : "(unnamed)",
                             PRG32_COLOR_WHITE,
                             0);
        }
        int clear_y = 40 + count * 18;
        prg32_gfx_text8(8, clear_y, choice == count ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(40, clear_y, "NO DEFAULT", PRG32_COLOR_WHITE, 0);
        draw_setup_status(144);
        prg32_gfx_text8(8, 216, "SELECT/B SAVE  A BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static int autoload_cartridge(void) {
    uint8_t slots[PRG32_CART_SLOT_COUNT];
    int count = stored_slots(slots, PRG32_CART_SLOT_COUNT);
    if (count <= 0) {
        return -1;
    }

    int default_slot = prg32_cart_default_slot();
    if (default_slot >= 0) {
        return prg32_cart_select_slot((uint8_t)default_slot);
    }
    if (count == 1) {
        return prg32_cart_select_slot(slots[0]);
    }
    return -1;
}

typedef enum {
    SETUP_AUDIO_NONE,
    SETUP_AUDIO_PWM,
    SETUP_AUDIO_I2S_MONO,
    SETUP_AUDIO_I2S_STEREO,
} setup_audio_output_t;

static uint8_t g_setup_audio_volume = 192;
static setup_audio_output_t g_setup_audio_output = SETUP_AUDIO_NONE;
static int g_setup_audio_detected;

static const uint8_t setup_audio_wave[] = {
    128, 176, 218, 245, 255, 245, 218, 176,
    128, 80, 38, 11, 1, 11, 38, 80,
    128, 176, 218, 245, 255, 245, 218, 176,
    128, 80, 38, 11, 1, 11, 38, 80,
};

static const char *audio_output_name(setup_audio_output_t output) {
    if (output == SETUP_AUDIO_I2S_STEREO) {
        return "STEREO I2S";
    }
    if (output == SETUP_AUDIO_I2S_MONO) {
        return "MONO I2S";
    }
    if (output == SETUP_AUDIO_PWM) {
        return "PWM BUZZER";
    }
    return "NONE";
}

static int setup_pin_matches(int gpio, int pin) {
    return gpio >= 0 && pin >= 0 && gpio == pin;
}

static int setup_audio_pin_conflicts(int gpio) {
    if (gpio < 0) {
        return 0;
    }
    return setup_pin_matches(gpio, PRG32_PIN_LCD_MOSI) ||
           setup_pin_matches(gpio, PRG32_PIN_LCD_MISO) ||
           setup_pin_matches(gpio, PRG32_PIN_LCD_SCLK) ||
           setup_pin_matches(gpio, PRG32_PIN_LCD_CS) ||
           setup_pin_matches(gpio, PRG32_PIN_LCD_DC) ||
           setup_pin_matches(gpio, PRG32_PIN_LCD_RST) ||
           setup_pin_matches(gpio, PRG32_PIN_LCD_BL) ||
           setup_pin_matches(gpio, PRG32_PIN_BTN_LEFT) ||
           setup_pin_matches(gpio, PRG32_PIN_BTN_RIGHT) ||
           setup_pin_matches(gpio, PRG32_PIN_BTN_UP) ||
           setup_pin_matches(gpio, PRG32_PIN_BTN_DOWN) ||
           setup_pin_matches(gpio, PRG32_PIN_BTN_A) ||
           setup_pin_matches(gpio, PRG32_PIN_BTN_B) ||
           setup_pin_matches(gpio, PRG32_PIN_BTN_SELECT) ||
           setup_pin_matches(gpio, PRG32_PIN_BUZZER);
}

static int setup_i2s_pins_safe(void) {
    if (CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO < 0 ||
        CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO < 0 ||
        CONFIG_PRG32_AUDIO_I2S_DATA_GPIO < 0) {
        return 0;
    }
    return !setup_audio_pin_conflicts(CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO) &&
           !setup_audio_pin_conflicts(CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO) &&
           !setup_audio_pin_conflicts(CONFIG_PRG32_AUDIO_I2S_DATA_GPIO) &&
           !setup_audio_pin_conflicts(CONFIG_PRG32_AUDIO_I2S_SD_GPIO);
}

static setup_audio_output_t detect_audio_output(void) {
    if (g_setup_audio_detected) {
        return g_setup_audio_output;
    }
    g_setup_audio_detected = 1;
    g_setup_audio_output = SETUP_AUDIO_NONE;

#if CONFIG_PRG32_AUDIO_ENABLED
    if (setup_i2s_pins_safe() && prg32_audio_init(NULL)) {
        prg32_audio_set_master_volume(g_setup_audio_volume);
        g_setup_audio_output =
            prg32_audio_get_mode() == PRG32_AUDIO_MODE_STEREO
                ? SETUP_AUDIO_I2S_STEREO
                : SETUP_AUDIO_I2S_MONO;
        return g_setup_audio_output;
    }
    prg32_audio_shutdown();
#endif

    if (PRG32_PIN_BUZZER >= 0) {
        g_setup_audio_output = SETUP_AUDIO_PWM;
    }
    return g_setup_audio_output;
}

static void setup_audio_prepare_i2s_test(void) {
    prg32_instrument_desc_t inst = {
        .sample_id = 63,
        .default_volume = g_setup_audio_volume,
        .default_pan = PRG32_AUDIO_PAN_CENTER,
        .attack = 0,
        .decay = 0,
        .sustain = 255,
        .release = 0,
    };
    prg32_audio_register_sample(63,
                                setup_audio_wave,
                                sizeof(setup_audio_wave),
                                60,
                                PRG32_AUDIO_SAMPLE_LOOP,
                                0,
                                sizeof(setup_audio_wave));
    prg32_audio_register_instrument(31, &inst);
}

static void play_audio_test_tune(setup_audio_output_t output) {
    static const uint16_t freq[] = { 262, 330, 392, 523 };
    static const uint8_t notes[] = { 60, 64, 67, 72 };
    uint16_t duty = (uint16_t)((512u * g_setup_audio_volume) / 255u);
    if (duty == 0) {
        duty = 1;
    }

    if (output == SETUP_AUDIO_I2S_MONO || output == SETUP_AUDIO_I2S_STEREO) {
        setup_audio_prepare_i2s_test();
        for (size_t i = 0; i < sizeof(notes); ++i) {
            prg32_audio_led_vu_level((uint8_t)(72 + i * 48));
            prg32_audio_note_on(0, 31, notes[i], g_setup_audio_volume);
            vTaskDelay(pdMS_TO_TICKS(130));
            prg32_audio_note_off(0);
            vTaskDelay(pdMS_TO_TICKS(35));
        }
        prg32_audio_led_vu_level(0);
        return;
    }

    if (output == SETUP_AUDIO_PWM) {
        for (size_t i = 0; i < sizeof(freq) / sizeof(freq[0]); ++i) {
            prg32_audio_led_vu_level((uint8_t)(72 + i * 48));
            prg32_audio_tone(freq[i], 110, duty);
            vTaskDelay(pdMS_TO_TICKS(35));
        }
        prg32_audio_led_vu_level(0);
    }
}

static void draw_volume_bar(int x, int y, uint8_t volume) {
    int width = (int)volume * 120 / 255;
    prg32_gfx_rect(x, y, 124, 12, PRG32_COLOR_BLUE);
    prg32_gfx_rect(x + 2, y + 2, width, 8, PRG32_COLOR_GREEN);
}

static void audio_menu(void) {
    int choice = 0;
    const int rows = 4;
    setup_audio_output_t output = detect_audio_output();
    prg32_input_wait_released(SETUP_KEYS);
    uint32_t last = 0;
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
            choice--;
        }
        if ((input & PRG32_BTN_DOWN) &&
            !(last & PRG32_BTN_DOWN) &&
            choice + 1 < rows) {
            choice++;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_input_wait_released(SETUP_CANCEL);
            return;
        }
        if ((input & PRG32_BTN_LEFT) && !(last & PRG32_BTN_LEFT) &&
            choice == 0 && g_setup_audio_volume >= 8) {
            g_setup_audio_volume -= 8;
            prg32_audio_set_master_volume(g_setup_audio_volume);
        }
        if ((input & PRG32_BTN_RIGHT) && !(last & PRG32_BTN_RIGHT) &&
            choice == 0 && g_setup_audio_volume <= 247) {
            g_setup_audio_volume += 8;
            prg32_audio_set_master_volume(g_setup_audio_volume);
        }
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            if (choice == 1 && prg32_rgb_led_available()) {
                prg32_audio_led_vu_enable(!prg32_audio_led_vu_enabled());
            } else if (choice == 2) {
                play_audio_test_tune(output);
            } else if (choice == 3) {
                prg32_input_wait_released(SETUP_ACCEPT);
                return;
            }
            prg32_input_wait_released(SETUP_ACCEPT);
        }

        char line[72];
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "AUDIO MENU", PRG32_COLOR_WHITE, 0);
        snprintf(line, sizeof(line), "OUTPUT: %s", audio_output_name(output));
        prg32_gfx_text8(8, 36, line, PRG32_COLOR_CYAN, 0);
        if (output == SETUP_AUDIO_I2S_MONO || output == SETUP_AUDIO_I2S_STEREO) {
            snprintf(line,
                     sizeof(line),
                     "I2S BCLK=%d LRCLK=%d DATA=%d",
                     CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO,
                     CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO,
                     CONFIG_PRG32_AUDIO_I2S_DATA_GPIO);
            prg32_gfx_text8(8, 52, line, PRG32_COLOR_CYAN, 0);
        } else if (output == SETUP_AUDIO_PWM) {
            snprintf(line, sizeof(line), "BUZZER GPIO=%d", PRG32_PIN_BUZZER);
            prg32_gfx_text8(8, 52, line, PRG32_COLOR_CYAN, 0);
        } else {
            prg32_gfx_text8(8, 52, "NO AUDIO OUTPUT DETECTED", PRG32_COLOR_YELLOW, 0);
        }

        snprintf(line, sizeof(line), "VOLUME: %u", g_setup_audio_volume);
        prg32_gfx_text8(8, 82, choice == 0 ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, 82, line, PRG32_COLOR_WHITE, 0);
        draw_volume_bar(168, 82, g_setup_audio_volume);

        snprintf(line,
                 sizeof(line),
                 "RGB V-METER: %s%s",
                 prg32_audio_led_vu_enabled() ? "ON" : "OFF",
                 prg32_rgb_led_available() ? "" : " (NO LED)");
        prg32_gfx_text8(8, 110, choice == 1 ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, 110, line, PRG32_COLOR_WHITE, 0);

        prg32_gfx_text8(8, 138, choice == 2 ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, 138, "PLAY TEST TUNE", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 166, choice == 3 ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, 166, "BACK", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 216, "LEFT/RIGHT VOLUME  SELECT/B OK  A BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static prg32_band_mode_t next_band_mode(prg32_band_mode_t mode, int delta) {
    int next = (int)mode + delta;
    if (next < (int)PRG32_BAND_MODE_NONE) {
        next = (int)PRG32_BAND_MODE_CUSTOM;
    }
    if (next > (int)PRG32_BAND_MODE_CUSTOM) {
        next = (int)PRG32_BAND_MODE_NONE;
    }
    return (prg32_band_mode_t)next;
}

static void developer_menu(void) {
    int choice = 0;
    const int rows = 3;
    prg32_input_wait_released(SETUP_KEYS);
    uint32_t last = 0;
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
            choice--;
        }
        if ((input & PRG32_BTN_DOWN) &&
            !(last & PRG32_BTN_DOWN) &&
            choice + 1 < rows) {
            choice++;
        }
        if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
            prg32_band_save_config();
            prg32_input_wait_released(SETUP_CANCEL);
            return;
        }

        int adjust = 0;
        if ((input & PRG32_BTN_LEFT) && !(last & PRG32_BTN_LEFT)) {
            adjust = -1;
        }
        if ((input & PRG32_BTN_RIGHT) && !(last & PRG32_BTN_RIGHT)) {
            adjust = 1;
        }
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            if (choice == 2) {
                prg32_band_save_config();
                prg32_input_wait_released(SETUP_ACCEPT);
                return;
            }
            adjust = 1;
        }
        if (adjust != 0 && choice < 2) {
            uint8_t band = choice == 0 ? PRG32_BAND_TOP : PRG32_BAND_BOTTOM;
            prg32_band_set_mode(band, next_band_mode(prg32_band_mode(band), adjust));
            if (prg32_band_mode(band) == PRG32_BAND_MODE_CUSTOM) {
                prg32_band_set_text(band,
                                    band == PRG32_BAND_TOP
                                        ? "PRG32 TOP STATUS"
                                        : "PRG32 BOTTOM STATUS");
            }
            prg32_band_save_config();
            prg32_input_wait_released(SETUP_ACCEPT | SETUP_ADJUST);
        }

        char line[64];
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "DEVELOPER MENU", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8,
                         28,
                         "GAME VIEWPORT: 320x200",
                         PRG32_COLOR_CYAN,
                         0);
        prg32_gfx_text8(8,
                         44,
                         "BANDS ARE STATUS OVERLAYS",
                         PRG32_COLOR_CYAN,
                         0);
        snprintf(line,
                 sizeof(line),
                 "TOP BAND: %s",
                 prg32_band_mode_name(prg32_band_mode(PRG32_BAND_TOP)));
        prg32_gfx_text8(8, 80, choice == 0 ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, 80, line, PRG32_COLOR_WHITE, 0);
        snprintf(line,
                 sizeof(line),
                 "BOTTOM BAND: %s",
                 prg32_band_mode_name(prg32_band_mode(PRG32_BAND_BOTTOM)));
        prg32_gfx_text8(8, 104, choice == 1 ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, 104, line, PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 128, choice == 2 ? ">" : " ", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(24, 128, "SAVE AND BACK", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8,
                         164,
                         "LEFT/RIGHT OR SELECT/B CYCLE",
                         PRG32_COLOR_YELLOW,
                         0);
        prg32_gfx_text8(8, 180, "A BACK", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static void about_menu(void) {
    prg32_input_wait_released(SETUP_KEYS);
    uint32_t last = 0;
    char version_line[40];
    snprintf(version_line, sizeof(version_line), "FIRMWARE %s", PRG32_FIRMWARE_VERSION);
    while (1) {
        uint32_t input = prg32_input_read_menu();
        if (((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) ||
            ((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            prg32_input_wait_released(SETUP_ACCEPT | SETUP_CANCEL);
            return;
        }

        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "ABOUT PRG32", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 32, "RETRO GAMING & CODING", PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(8, 48, version_line, PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 64, "AUTHORS AND CONTRIBUTORS", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, 88, "RAFFAELE MONTELLA", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 104, "UNIPARTHENOPE", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 120, "ACADEMIC SUPERVISOR / PROJECT LEAD", PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(8, 152, "IVAN CAFIERO & SIMONE BOSCAGLIA", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 168, "UNIPARTHENOPE", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 184, "COMPUTER SCIENCE STUDENTS", PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(8, 224, "A / SELECT / B BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static int setup_menu(void) {
    printf("setup_menu => start\n");
    int choice = 0;
    printf("setup_menu => input_wait_released(SETUP_KEYS)\n");
    prg32_input_wait_released(SETUP_KEYS);
    while (1) {
        setup_option_t options[11];
        int option_count = 0;
        printf("setup_menu => prg32_cart_stored_count\n");
        int cart_count = prg32_cart_stored_count();
        if (cart_count > 0) {
            options[option_count++] = (setup_option_t){
                SETUP_OPTION_RUN_CART,
                "RUN CARTRIDGE",
            };
            options[option_count++] = (setup_option_t){
                SETUP_OPTION_DEFAULT_CART,
                "DEFAULT CARTRIDGE",
            };
        }
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_WIFI,
            "WIFI SETUP",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_STORE_CONFIG,
            "CARTRIDGE STORE",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_STORE_BROWSE,
            "BROWSE STORE",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_AUDIO,
            "AUDIO SETUP",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_DEVELOPER,
            "DEVELOPER MENU",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_PERFORMANCE,
            "PERFORMANCE TEST",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_ABOUT,
            "ABOUT PRG32",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_EXIT,
            "EXIT SETUP",
        };
        if (choice >= option_count) {
            choice = option_count - 1;
        }
        printf("setup_menu => prg32_input_read_menu()\n");
        uint32_t last = prg32_input_read_menu();
        while (1) {
            printf("setup_menu => prg32_input_read_menu()\n");
            uint32_t input = prg32_input_read_menu();
            if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
                choice--;
            }
            if ((input & PRG32_BTN_DOWN) &&
                !(last & PRG32_BTN_DOWN) &&
                choice + 1 < option_count) {
                choice++;
            }
            if ((input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
                printf("setup_menu => input_wait_released(SETUP_CANCEL)\n");
                prg32_input_wait_released(SETUP_CANCEL);
                printf("setup_menu => cart_is_loaded()\n");
                printf("setup_menu => autoload_cartridge()\n");
                if (prg32_cart_is_loaded() || autoload_cartridge() == 0) {
                    return 0;
                }
                return -1;
            }
            if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
                ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
                setup_option_id_t selected = options[choice].id;
                printf("setup_menu => input_wait_released(SETUP_ACCEPT)\n");
                prg32_input_wait_released(SETUP_ACCEPT);
                if (selected == SETUP_OPTION_RUN_CART) {
                    if (cartridge_picker("RUN CARTRIDGE", true) == 0) {
                        return 0;
                    }
                    break;
                }
                if (selected == SETUP_OPTION_DEFAULT_CART) {
                    default_cartridge_picker();
                    break;
                }
                if (selected == SETUP_OPTION_WIFI) {
                    prg32_wifi_setup_run();
                    prg32_scores_api_start();
                    break;
                }
                if (selected == SETUP_OPTION_STORE_CONFIG) {
                    prg32_setup_store_run();
                    break;
                }
                if (selected == SETUP_OPTION_STORE_BROWSE) {
                    prg32_setup_store_browse_run();
                    if (prg32_cart_is_loaded()) {
                        return 0;
                    }
                    break;
                }
                if (selected == SETUP_OPTION_AUDIO) {
                    audio_menu();
                    break;
                }
                if (selected == SETUP_OPTION_DEVELOPER) {
                    developer_menu();
                    break;
                }
                if (selected == SETUP_OPTION_PERFORMANCE) {
                    start_performance_http_api();
                    prg32_performance_test_run();
                    break;
                }
                if (selected == SETUP_OPTION_ABOUT) {
                    about_menu();
                    break;
                }
                if (selected == SETUP_OPTION_EXIT) {
                    if (prg32_cart_is_loaded() || autoload_cartridge() == 0) {
                        return 0;
                    }
                    return -1;
                }
            }

            prg32_gfx_clear(PRG32_COLOR_BLACK);
            prg32_gfx_text8(8, 8, "PRG32 SETUP", PRG32_COLOR_WHITE, 0);
            draw_setup_status(28);
            draw_setup_resources(76);
            draw_cartridge_status(92);
            for (int i = 0; i < option_count; ++i) {
                int y = 120 + i * 11;
                prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
                prg32_gfx_text8(24, y, options[i].label, PRG32_COLOR_WHITE, 0);
            }
            prg32_gfx_text8(8,
                             228,
                             "UP/DOWN MOVE  SELECT/B OK  A BACK",
                             PRG32_COLOR_CYAN,
                             0);
            printf("setup_menu => prg32_gfx_present()\n");
            prg32_gfx_present();
            last = input;
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
}

void prg32_init(void) {
    printf("prg32_init()\n");
    printf("prg32_init => prg32_display_init()\n");
    prg32_display_init();
    printf("prg32_init => prg32_rgb_led_init()\n");
    prg32_rgb_led_init(PRG32_PIN_RGB_LED);
    printf("prg32_init => prg32_audio_pwm_init()\n");
    prg32_audio_pwm_init();
    printf("prg32_init => prg32_splash_show_default()\n");
    prg32_splash_show_default();
    printf("prg32_init => prg32_input_init()\n");
    prg32_input_init();
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
#ifdef PRG32_STORE_SERVER_URL
    char current_url[PRG32_STORE_URL_MAX_LEN];
    if (prg32_store_url_get(current_url, sizeof(current_url)) != 0) {
        prg32_store_url_set(PRG32_STORE_SERVER_URL);
    }
#endif
    printf("prg32_init => prg32_abi_exports_keep()\n");
    prg32_abi_exports_keep();
    printf("prg32_init => prg32_cart_init()\n");
    prg32_cart_init();
    printf("prg32_init => prg32_band_load_config()\n");
    prg32_band_load_config();
    printf("prg32_init => prg32_input_read_menu()\n");
    uint32_t boot_input = prg32_input_read_menu();
    printf("prg32_init => prg32_cart_stored_count()\n");
    int stored_count = prg32_cart_stored_count();
    printf("prg32_init => prg32_wifi_setup_requested()\n");
    printf("prg32_init => prg32_cart_default_slot()\n");
    bool setup_requested =
        PRG32_BOOT_SETUP_MODE ||
        prg32_wifi_setup_requested() ||
        ((boot_input & PRG32_BTN_A) && (boot_input & PRG32_BTN_B)) ||
        stored_count == 0 ||
        (stored_count > 1 && prg32_cart_default_slot() < 0);
    printf("prg32_init => autoload_cartridge()\n");
    if (!setup_requested && autoload_cartridge() != 0) {
        setup_requested = true;
    }
    if (setup_requested) {
        prg32_gfx_set_fullscreen(1);
        printf("prg32_init => wifi_scores_init()\n");
        prg32_wifi_scores_init();
        printf("prg32_init => scores_api_start()\n");
        prg32_scores_api_start();
        printf("prg32_init => setup_menu()\n");
        setup_menu();
    }

    printf("prg32_init => wifi_scores_init()\n");
    printf("prg32_init => scores_api_start()\n");
#if PRG32_WIFI_SCORES_ENABLE
    prg32_wifi_scores_init();
    prg32_scores_api_start();
#endif
    printf("prg32_init => cart_is_loaded()\n");
    printf("prg32_init => cart_stored_count()\n");
    if (!prg32_cart_is_loaded() && prg32_cart_stored_count() > 0) {
        printf("prg32_init => autoload_cartridge()\n");
        //autoload_cartridge();
    }
    prg32_gfx_set_fullscreen(0);
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_present();
    prg32_set_mode(PRG32_DEFAULT_MODE);
    printf("prg32_init => cart_is_loaded()\n");
    if (!prg32_cart_is_loaded()) {
        printf("prg32_init => console_clear()\n");
        prg32_console_clear();
    }
}
