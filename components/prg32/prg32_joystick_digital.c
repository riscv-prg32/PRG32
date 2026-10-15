#include "prg32.h"
#include "prg32_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "prg32_joystick_digital";

static void digital_pin_init(const char *name, int p) {
    if (p < 0) {
        return;
    }
    ESP_LOGI(TAG, "joystick digital init: %s GPIO%d reset", name, p);
    esp_err_t err = gpio_reset_pin(p);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "joystick digital init: %s GPIO%d input", name, p);
        err = gpio_set_direction(p, GPIO_MODE_INPUT);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "joystick digital init: %s GPIO%d pullup", name, p);
        err = gpio_set_pull_mode(p, GPIO_PULLUP_ONLY);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "joystick digital init: %s GPIO%d failed: %s",
                 name,
                 p,
                 esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(1));
}

void prg32_joystick_digital_init(void) {
#if !CONFIG_PRG32_DISPLAY_QEMU_RGB && defined(CONFIG_PRG32_JOYSTICK_DIGITAL) && CONFIG_PRG32_JOYSTICK_DIGITAL
    ESP_LOGI(TAG, "Initializing digital joystick directional pins");
    digital_pin_init("LEFT", PRG32_PIN_BTN_LEFT);
    digital_pin_init("RIGHT", PRG32_PIN_BTN_RIGHT);
    digital_pin_init("UP", PRG32_PIN_BTN_UP);
    digital_pin_init("DOWN", PRG32_PIN_BTN_DOWN);
#endif
}

uint32_t prg32_joystick_digital_read(void) {
    uint32_t v = 0;
#if !CONFIG_PRG32_DISPLAY_QEMU_RGB && defined(CONFIG_PRG32_JOYSTICK_DIGITAL) && CONFIG_PRG32_JOYSTICK_DIGITAL
    if (PRG32_PIN_BTN_LEFT >= 0 && !gpio_get_level(PRG32_PIN_BTN_LEFT)) {
        v |= PRG32_BTN_LEFT;
    }
    if (PRG32_PIN_BTN_RIGHT >= 0 && !gpio_get_level(PRG32_PIN_BTN_RIGHT)) {
        v |= PRG32_BTN_RIGHT;
    }
    if (PRG32_PIN_BTN_UP >= 0 && !gpio_get_level(PRG32_PIN_BTN_UP)) {
        v |= PRG32_BTN_UP;
    }
    if (PRG32_PIN_BTN_DOWN >= 0 && !gpio_get_level(PRG32_PIN_BTN_DOWN)) {
        v |= PRG32_BTN_DOWN;
    }
#endif
    return v;
}
