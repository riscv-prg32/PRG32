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

void prg32_input_init(void) {
    printf("prg32_input_init START\n");
    pin_in("LEFT", PRG32_PIN_BTN_LEFT);
    pin_in("RIGHT", PRG32_PIN_BTN_RIGHT);
    pin_in("UP", PRG32_PIN_BTN_UP);
    pin_in("DOWN", PRG32_PIN_BTN_DOWN);
    pin_in("A", PRG32_PIN_BTN_A);
    pin_in("B", PRG32_PIN_BTN_B);
    printf("prg32_input_init PIN_BTN_START\n");
    pin_in("START", PRG32_PIN_BTN_START);
    printf("prg32_input_init PIN_BTN_SELECT\n");
    if (PRG32_PIN_BTN_SELECT != PRG32_PIN_BTN_START) {
        pin_in("SELECT", PRG32_PIN_BTN_SELECT);
    }

    pin_in("SETUP", PRG32_PIN_SETUP);
    printf("prg32_input_init END\n");
    //vTaskDelay(pdMS_TO_TICKS(1));
}

uint32_t prg32_input_read(void) {
    return prg32_controller_read();
}

uint32_t prg32_input_read_player(uint8_t player) {
    if (player == 2) {
        return 0;
    }
    return prg32_input_read() & 0x7fu;
}

uint32_t prg32_input_read_menu(void) {
    return prg32_input_read() & 0x7fu;
}

void prg32_input_wait_released(uint32_t mask) {
    printf("prg32_input_wait_released START\n");
    printf("prg32_input_wait_released mask=%ld\n", mask);
    int stable = 0;
    while (stable < 2) {
        printf("prg32_input_wait_released => prg32_input_read_menu()\n");
        uint32_t input_read_menu = prg32_input_read_menu();
        printf("prg32_input_read_menu() = %ld\n", input_read_menu);
        if ((input_read_menu & mask) == 0) {
            stable++;
        } else {
            stable = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
