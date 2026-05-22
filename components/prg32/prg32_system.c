#include "prg32.h"
#include "prg32_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void prg32_display_init(void);
void prg32_input_init(void);
void prg32_audio_pwm_init(void);
void prg32_abi_exports_keep(void);
void prg32_device_demo_run(void);

#ifndef PRG32_BOOT_SETUP_MODE
#define PRG32_BOOT_SETUP_MODE 0
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
    SETUP_OPTION_DEVELOPER,
    SETUP_OPTION_DEMO,
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
        if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
            ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
            prg32_input_wait_released(SETUP_ACCEPT);
            return prg32_cart_select_slot(slots[choice]);
        }
        if (!force_choice && (input & PRG32_BTN_A) && !(last & PRG32_BTN_A)) {
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
        prg32_gfx_text8(8, 216, "UP/DOWN CHOOSE  SELECT/B RUN  A BACK", PRG32_COLOR_CYAN, 0);
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
        prg32_gfx_text8(8, 64, "AUTHORS AND CONTRIBUTORS", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, 88, "RAFFAELE MONTELLA", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 104, "UNIPARTHENOPE", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 120, "ACADEMIC SUPERVISOR / PROJECT LEAD", PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(8, 152, "IVAN CAFIERO", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 168, "UNIPARTHENOPE", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 184, "COMPUTER SCIENCE STUDENT", PRG32_COLOR_CYAN, 0);
        prg32_gfx_text8(8, 224, "A / SELECT / B BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        last = input;
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static int setup_menu(void) {
    int choice = 0;
    prg32_input_wait_released(SETUP_KEYS);
    while (1) {
        setup_option_t options[7];
        int option_count = 0;
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
            SETUP_OPTION_DEVELOPER,
            "DEVELOPER MENU",
        };
        options[option_count++] = (setup_option_t){
            SETUP_OPTION_DEMO,
            "DEVICE DEMO",
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

        uint32_t last = prg32_input_read_menu();
        while (1) {
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
                prg32_input_wait_released(SETUP_CANCEL);
                if (prg32_cart_is_loaded() || autoload_cartridge() == 0) {
                    return 0;
                }
                return -1;
            }
            if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
                ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
                setup_option_id_t selected = options[choice].id;
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
                if (selected == SETUP_OPTION_DEVELOPER) {
                    developer_menu();
                    break;
                }
                if (selected == SETUP_OPTION_DEMO) {
                    prg32_device_demo_run();
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
            draw_cartridge_status(76);
            for (int i = 0; i < option_count; ++i) {
                int y = 108 + i * 16;
                prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
                prg32_gfx_text8(24, y, options[i].label, PRG32_COLOR_WHITE, 0);
            }
            prg32_gfx_text8(8,
                             228,
                             "UP/DOWN MOVE  SELECT/B OK  A BACK",
                             PRG32_COLOR_CYAN,
                             0);
            prg32_gfx_present();
            last = input;
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
}

void prg32_init(void) {
    prg32_display_init();
    prg32_audio_pwm_init();
    prg32_splash_show_default();
    prg32_input_init();
    prg32_abi_exports_keep();
    prg32_cart_init();
    prg32_band_load_config();
    uint32_t boot_input = prg32_input_read_menu();
    int stored_count = prg32_cart_stored_count();
    bool setup_requested =
        PRG32_BOOT_SETUP_MODE ||
        prg32_wifi_setup_requested() ||
        ((boot_input & PRG32_BTN_A) && (boot_input & PRG32_BTN_B)) ||
        stored_count == 0 ||
        (stored_count > 1 && prg32_cart_default_slot() < 0);

    if (!setup_requested && autoload_cartridge() != 0) {
        setup_requested = true;
    }

    if (setup_requested) {
        prg32_gfx_set_fullscreen(1);
        if (stored_count == 0) {
            prg32_wifi_scores_init();
            prg32_scores_api_start();
        }
        setup_menu();
    }

#if PRG32_WIFI_SCORES_ENABLE
    prg32_wifi_scores_init();
    prg32_scores_api_start();
#endif

    if (!prg32_cart_is_loaded() && prg32_cart_stored_count() > 0) {
        autoload_cartridge();
    }
    prg32_gfx_set_fullscreen(0);
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_present();
    prg32_set_mode(PRG32_DEFAULT_MODE);
    if (!prg32_cart_is_loaded()) {
        prg32_console_clear();
    }
}
