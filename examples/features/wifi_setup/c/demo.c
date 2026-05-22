#include "prg32.h"

static uint32_t last_input;

void wifi_setup_c_init(void) {
    last_input = 0;
}

void wifi_setup_c_update(void) {
    uint32_t input = prg32_input_read_menu();
    if ((((input & PRG32_BTN_SELECT) && !(last_input & PRG32_BTN_SELECT))) ||
        ((input & PRG32_BTN_B) && !(last_input & PRG32_BTN_B))) {
        prg32_wifi_setup_run();
    }
    last_input = input;
}

void wifi_setup_c_draw(void) {
    prg32_wifi_mode_t mode = prg32_wifi_current_mode();
    const char *name = "OFF";
    if (mode == PRG32_WIFI_MODE_STA) {
        name = "STA";
    } else if (mode == PRG32_WIFI_MODE_AP) {
        name = "AP";
    } else if (mode == PRG32_WIFI_MODE_APSTA) {
        name = "AP+STA";
    }
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    prg32_gfx_text8(8, 8, "WIFI SETUP C", PRG32_COLOR_WHITE, 0);
    prg32_gfx_text8(8, 32, "SELECT/B OPEN SETUP", PRG32_COLOR_CYAN, 0);
    prg32_gfx_text8(8, 56, "MODE:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(56, 56, name, PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 72, "IP:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(40, 72, prg32_wifi_current_ip(), PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(8, 88, "SSID:", PRG32_COLOR_GREEN, 0);
    prg32_gfx_text8(56, 88, prg32_wifi_current_ssid(), PRG32_COLOR_GREEN, 0);
}
