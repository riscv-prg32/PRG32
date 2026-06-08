#include "prg32.h"
#include "prg32_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define STORE_ACCEPT (PRG32_BTN_SELECT | PRG32_BTN_B)
#define STORE_CANCEL PRG32_BTN_A

static void wait_and_show(const char *line, uint32_t ms) {
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "CARTRIDGE STORE", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 48, line, PRG32_COLOR_CYAN, 0);
    prg32_gfx_present();
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void normalize_store_url(char *url, size_t cap) {
    if (!url || cap == 0 || strstr(url, "://")) {
        return;
    }
    char tmp[PRG32_STORE_URL_MAX_LEN];
    snprintf(tmp, sizeof(tmp), "http://%s:%d", url, PRG32_STORE_DEFAULT_PORT);
    snprintf(url, cap, "%s", tmp);
}

void prg32_setup_store_run(void) {
    int choice = 0;
    static const char *items[] = {
        "AUTO-DISCOVER",
        "MANUAL ENTRY",
        "CLEAR SAVED URL",
        "BACK",
    };
    prg32_input_wait_released(STORE_ACCEPT | STORE_CANCEL);
    while (1) {
        char current[PRG32_STORE_URL_MAX_LEN];
        int has_current = prg32_store_url_resolve(current, sizeof(current)) == 0;
        uint32_t last = prg32_input_read_menu();
        while (1) {
            uint32_t input = prg32_input_read_menu();
            if ((input & PRG32_BTN_UP) && !(last & PRG32_BTN_UP) && choice > 0) {
                choice--;
            }
            if ((input & PRG32_BTN_DOWN) && !(last & PRG32_BTN_DOWN) && choice < 3) {
                choice++;
            }
            if ((input & STORE_CANCEL) && !(last & STORE_CANCEL)) {
                prg32_input_wait_released(STORE_CANCEL);
                return;
            }
            if (((input & PRG32_BTN_SELECT) && !(last & PRG32_BTN_SELECT)) ||
                ((input & PRG32_BTN_B) && !(last & PRG32_BTN_B))) {
                prg32_input_wait_released(STORE_ACCEPT);
                if (choice == 0) {
                    char found[PRG32_STORE_URL_MAX_LEN];
                    wait_and_show("SCANNING...", 10);
                    if (prg32_store_discover(found, sizeof(found)) == 0) {
                        while (1) {
                            uint32_t save_input = prg32_input_read_menu();
                            prg32_gfx_clear(PRG32_COLOR_BLACK);
                            prg32_gfx_text8(8, 8, "CARTRIDGE STORE", PRG32_COLOR_WHITE, 0);
                            prg32_gfx_text8(8, 40, "FOUND:", PRG32_COLOR_GREEN, 0);
                            prg32_gfx_text8(8, 58, found, PRG32_COLOR_WHITE, 0);
                            prg32_gfx_text8(8, 216, "SELECT SAVE  A DISCARD", PRG32_COLOR_CYAN, 0);
                            prg32_gfx_present();
                            if (save_input & PRG32_BTN_SELECT) {
                                prg32_store_url_set(found);
                                prg32_input_wait_released(STORE_ACCEPT);
                                wait_and_show("SAVED", 1000);
                                break;
                            }
                            if (save_input & STORE_CANCEL) {
                                prg32_input_wait_released(STORE_CANCEL);
                                break;
                            }
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                    } else {
                        wait_and_show("NOT FOUND", 2000);
                    }
                    break;
                }
                if (choice == 1) {
                    char url[PRG32_STORE_URL_MAX_LEN] = "";
                    if (prg32_text_input(url, sizeof(url), "STORE URL") >= 0 && url[0]) {
                        normalize_store_url(url, sizeof(url));
                        if (prg32_store_url_set(url) == 0) {
                            wait_and_show("SAVED", 1000);
                        } else {
                            wait_and_show("SAVE FAILED", 1000);
                        }
                    }
                    break;
                }
                if (choice == 2) {
                    prg32_store_url_clear();
                    wait_and_show("CLEARED", 1000);
                    break;
                }
                return;
            }
            prg32_gfx_clear(PRG32_COLOR_BLACK);
            prg32_gfx_text8(8, 8, "CARTRIDGE STORE", PRG32_COLOR_WHITE, 0);
            for (int i = 0; i < 4; ++i) {
                int y = 42 + i * 18;
                prg32_gfx_text8(8, y, i == choice ? ">" : " ", PRG32_COLOR_GREEN, 0);
                prg32_gfx_text8(24, y, items[i], PRG32_COLOR_WHITE, 0);
            }
            prg32_gfx_text8(8, 144, "CURRENT:", PRG32_COLOR_YELLOW, 0);
            prg32_gfx_text8(8, 162, has_current ? current : "(not configured)", PRG32_COLOR_CYAN, 0);
            prg32_gfx_text8(8, 216, "UP/DOWN MOVE  SELECT/B OK  A BACK", PRG32_COLOR_CYAN, 0);
            prg32_gfx_present();
            last = input;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void prg32_setup_store_browse_run(void) {
    char url[PRG32_STORE_URL_MAX_LEN];
    if (prg32_store_url_resolve(url, sizeof(url)) != 0) {
        wait_and_show("CONFIGURE STORE FIRST", 2000);
        return;
    }
    char name[64] = "";
    wait_and_show("CONNECTING...", 10);
    if (prg32_store_ping(url, name, sizeof(name)) == 0) {
        prg32_gfx_clear(PRG32_COLOR_BLACK);
        prg32_gfx_text8(8, 8, "BROWSE STORE", PRG32_COLOR_WHITE, 0);
        prg32_gfx_text8(8, 40, name[0] ? name : "STORE AVAILABLE", PRG32_COLOR_GREEN, 0);
        prg32_gfx_text8(8, 64, "CATALOG BROWSER TODO", PRG32_COLOR_YELLOW, 0);
        prg32_gfx_text8(8, 216, "A / SELECT / B BACK", PRG32_COLOR_CYAN, 0);
        prg32_gfx_present();
        prg32_input_wait_released(STORE_ACCEPT | STORE_CANCEL);
        while (!(prg32_input_read_menu() & (STORE_ACCEPT | STORE_CANCEL))) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        prg32_input_wait_released(STORE_ACCEPT | STORE_CANCEL);
    } else {
        wait_and_show("UNAVAILABLE", 2000);
    }
}
