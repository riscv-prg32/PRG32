#include "prg32.h"
#include "prg32_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef PRG32_PIN_BTN_SELECT
#define PRG32_PIN_BTN_SELECT PRG32_PIN_BTN_START
#endif

#ifndef PRG32_PIN_P2_SELECT
#define PRG32_PIN_P2_SELECT PRG32_PIN_P2_START
#endif

static const char *TAG = "prg32_input";

static void pin_in(const char *name, int p) {
    if (p < 0) {
        return;
    }
    ESP_LOGI(TAG, "input init: %s GPIO%d reset", name, p);
    esp_err_t err = gpio_reset_pin(p);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "input init: %s GPIO%d input", name, p);
        err = gpio_set_direction(p, GPIO_MODE_INPUT);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "input init: %s GPIO%d pullup", name, p);
        err = gpio_set_pull_mode(p, GPIO_PULLUP_ONLY);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "input init: %s GPIO%d failed: %s",
                 name,
                 p,
                 esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(1));
}

void prg32_controller_bridge_init(void);

void prg32_input_init(void) {
    pin_in("LEFT", PRG32_PIN_BTN_LEFT);
    pin_in("RIGHT", PRG32_PIN_BTN_RIGHT);
    pin_in("UP", PRG32_PIN_BTN_UP);
    pin_in("DOWN", PRG32_PIN_BTN_DOWN);
    pin_in("A", PRG32_PIN_BTN_A);
    pin_in("B", PRG32_PIN_BTN_B);
    pin_in("START", PRG32_PIN_BTN_START);
    if (PRG32_PIN_BTN_SELECT != PRG32_PIN_BTN_START) {
        pin_in("SELECT", PRG32_PIN_BTN_SELECT);
    }
    pin_in("P2 LEFT", PRG32_PIN_P2_LEFT);
    pin_in("P2 RIGHT", PRG32_PIN_P2_RIGHT);
    pin_in("P2 UP", PRG32_PIN_P2_UP);
    pin_in("P2 DOWN", PRG32_PIN_P2_DOWN);
    pin_in("P2 A", PRG32_PIN_P2_A);
    pin_in("P2 B", PRG32_PIN_P2_B);
    pin_in("P2 START", PRG32_PIN_P2_START);
    if (PRG32_PIN_P2_SELECT != PRG32_PIN_P2_START) {
        pin_in("P2 SELECT", PRG32_PIN_P2_SELECT);
    }
    pin_in("SETUP", PRG32_PIN_SETUP);
    vTaskDelay(pdMS_TO_TICKS(1));
    prg32_controller_bridge_init();
}

uint32_t prg32_input_read(void) {
    return prg32_controller_read();
}

uint32_t prg32_input_read_player(uint8_t player) {
    uint32_t input = prg32_input_read();
    if (player == 2) {
        return (input >> 8) & 0x7fu;
    }
    return input & 0x7fu;
}

uint32_t prg32_input_read_menu(void) {
    uint32_t input = prg32_input_read();
    return (input & 0x7fu) | ((input >> 8) & 0x7fu);
}

void prg32_input_wait_released(uint32_t mask) {
    int stable = 0;
    while (stable < 2) {
        if ((prg32_input_read_menu() & mask) == 0) {
            stable++;
        } else {
            stable = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
