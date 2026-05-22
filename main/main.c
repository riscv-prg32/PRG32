#include <inttypes.h>
#include <stdio.h>

#include "prg32.h"
#include "prg32_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "prg32_main";

#define PRG32_FRAME_MS 33

#ifndef PRG32_BOOT_SIGNAL_ENABLE
#define PRG32_BOOT_SIGNAL_ENABLE 0
#endif

static void prg32_wait_for_frame_target(uint32_t *next_ms) {
    uint32_t now = prg32_ticks_ms();
    if (!next_ms) {
        return;
    }
    if (*next_ms == 0) {
        *next_ms = now;
    }
    int32_t late_ms = (int32_t)(now - *next_ms);
    if (late_ms > (int32_t)(PRG32_FRAME_MS * 4u)) {
        *next_ms = now;
    } else {
        *next_ms += PRG32_FRAME_MS;
    }
    now = prg32_ticks_ms();
    if ((int32_t)(*next_ms - now) > 0) {
        vTaskDelay(pdMS_TO_TICKS(*next_ms - now));
    }
}

#if PRG32_BOOT_SIGNAL_ENABLE
static void prg32_boot_signal(void) {
#if PRG32_PIN_LCD_BL >= 0
    const int on_level = PRG32_LCD_BACKLIGHT_ACTIVE_LEVEL ? 1 : 0;
    const int off_level = PRG32_LCD_BACKLIGHT_ACTIVE_LEVEL ? 0 : 1;
    if (gpio_set_direction(PRG32_PIN_LCD_BL, GPIO_MODE_OUTPUT) != ESP_OK) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        gpio_set_level(PRG32_PIN_LCD_BL, on_level);
        esp_rom_delay_us(300000);
        gpio_set_level(PRG32_PIN_LCD_BL, off_level);
        esp_rom_delay_us(300000);
    }
    gpio_set_level(PRG32_PIN_LCD_BL, on_level);
#endif
}
#endif

void app_main(void) {
    esp_rom_printf("PRG32 ROM: app_main entered\n");
#if PRG32_BOOT_SIGNAL_ENABLE
    prg32_boot_signal();
#endif
    printf("PRG32 boot: app_main entered\n");
    ESP_LOGI(TAG, "starting PRG32 runtime");
#if PRG32_BOOT_DIAGNOSTIC_DELAY_MS > 0
    vTaskDelay(pdMS_TO_TICKS(PRG32_BOOT_DIAGNOSTIC_DELAY_MS));
#endif

    prg32_init();
    if (!prg32_cart_is_loaded()) {
        prg32_console_clear();
        prg32_console_write("PRG32 READY: use setup to upload a cartridge\n");
    }
    ESP_LOGI(TAG, "PRG32 runtime initialized");
#if PRG32_DEBUG
    prg32_console_write("PRG32 DEBUG enabled\n");
#endif

    uint32_t cart_generation = 0;
    uint32_t last_idle_log_ms = 0;
    uint32_t next_frame_ms = prg32_ticks_ms();
    while (1) {
        uint32_t input_snapshot = prg32_controller_read();
        prg32_diag_set_input_state(input_snapshot);

        if (prg32_cart_is_loaded()) {
            uint32_t current_generation = prg32_cart_generation();
            if (current_generation != cart_generation) {
                cart_generation = current_generation;
                prg32_console_clear();
                prg32_cart_call_init();
            }
            prg32_cart_call_update();
            prg32_cart_call_draw();
#if PRG32_DEBUG
            prg32_debug_overlay_draw(1, 0, 0, input_snapshot, prg32_diag_frame_count());
#endif
            prg32_gfx_present();
            prg32_diag_increment_frame();
            prg32_wait_for_frame_target(&next_frame_ms);
        } else {
            uint32_t now_ms = prg32_ticks_ms();
            if (now_ms - last_idle_log_ms >= PRG32_IDLE_HEARTBEAT_MS) {
                last_idle_log_ms = now_ms;
                esp_rom_printf("PRG32 ROM: idle heartbeat input=0x%08" PRIx32 "\n",
                               input_snapshot);
                ESP_LOGI(TAG,
                         "idle heartbeat: no cartridge loaded, input=0x%08" PRIx32,
                         input_snapshot);
            }
            prg32_gfx_present();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
