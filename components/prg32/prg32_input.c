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
    ESP_LOGI(TAG, "prg32_input_init START");
#if defined(CONFIG_PRG32_JOYSTICK_ANALOG) && CONFIG_PRG32_JOYSTICK_ANALOG
    prg32_joystick_analog_init();
#elif defined(CONFIG_PRG32_JOYSTICK_DIGITAL) && CONFIG_PRG32_JOYSTICK_DIGITAL
    prg32_joystick_digital_init();
#endif

    pin_in("A", PRG32_PIN_BTN_A);
    pin_in("B", PRG32_PIN_BTN_B);
    pin_in("C", PRG32_PIN_BTN_C);
    pin_in("D", PRG32_PIN_BTN_D);
    pin_in("L", PRG32_PIN_BTN_L);
    pin_in("R", PRG32_PIN_BTN_R);
    pin_in("START", PRG32_PIN_BTN_START);
    if (PRG32_PIN_BTN_SELECT != PRG32_PIN_BTN_START) {
        pin_in("SELECT", PRG32_PIN_BTN_SELECT);
    }

    pin_in("SETUP", PRG32_PIN_SETUP);
    ESP_LOGI(TAG, "prg32_input_init END");
}

uint32_t prg32_input_read(void) {
    return prg32_controller_read();
}

uint32_t prg32_input_read_player(uint8_t player) {
    if (player == 2) {
        return 0;
    }
    return prg32_input_read() & PRG32_INPUT_BUTTONS_MASK;
}

/* One axis from the digital directions: -127, 0, or 127. */
static int32_t digital_axis(uint32_t negative, uint32_t positive) {
    uint32_t input = prg32_input_read();
    if ((input & negative) && !(input & positive)) {
        return -127;
    }
    if ((input & positive) && !(input & negative)) {
        return 127;
    }
    return 0;
}

int32_t prg32_input_axis_x(void) {
    int x = 0;
    prg32_joystick_analog_get_axes(&x, NULL);
    /* A resting stick still lets remote and QEMU keys steer. */
    if (x == 0) {
        return digital_axis(PRG32_BTN_LEFT, PRG32_BTN_RIGHT);
    }
    return x;
}

int32_t prg32_input_axis_y(void) {
    int y = 0;
    prg32_joystick_analog_get_axes(NULL, &y);
    if (y == 0) {
        return digital_axis(PRG32_BTN_UP, PRG32_BTN_DOWN);
    }
    return y;
}

int prg32_input_axes_raw(int *out_x, int *out_y) {
    return prg32_joystick_analog_get_raw(out_x, out_y);
}

int prg32_input_has_analog(void) {
    return prg32_joystick_analog_available();
}

uint32_t prg32_input_read_menu(void) {
    return prg32_input_read() & PRG32_INPUT_BUTTONS_MASK;
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
