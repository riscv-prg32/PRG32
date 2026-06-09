#include <inttypes.h>
#include <stdio.h>

#include "prg32.h"
#include "prg32_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/uart.h"

static const char *TAG = "prg32_main";

#define PRG32_FRAME_MS 33

#ifndef PRG32_BOOT_SIGNAL_ENABLE
#define PRG32_BOOT_SIGNAL_ENABLE 0
#endif

#ifndef CONFIG_PRG32_METRICS_ENABLE
#define CONFIG_PRG32_METRICS_ENABLE 0
#endif

#ifndef CONFIG_PRG32_METRICS_SERVER_URL
#define CONFIG_PRG32_METRICS_SERVER_URL "http://192.168.4.2:8080"
#endif

#ifndef CONFIG_PRG32_METRICS_BOARD_ID
#define CONFIG_PRG32_METRICS_BOARD_ID "prg32-board"
#endif

#ifndef CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES
#define CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES 1
#endif

#ifndef CONFIG_PRG32_METRICS_UPLOAD_PERIOD_MS
#define CONFIG_PRG32_METRICS_UPLOAD_PERIOD_MS 5000
#endif

#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "unknown"
#endif

#if CONFIG_PRG32_DISPLAY_QEMU_RGB
#define PRG32_DISPLAY_BACKEND_NAME "qemu-rgb"
#else
#define PRG32_DISPLAY_BACKEND_NAME "ili9341"
#endif

static char g_metrics_game_name[PRG32_CART_NAME_LEN] = "idle";
static prg32_metrics_config_t g_metrics_config;
static int g_metrics_run_active;

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
    int32_t delay_ms = (int32_t)(*next_ms - now);
    TickType_t ticks = delay_ms > 0 ? pdMS_TO_TICKS(delay_ms) : 0;
    if (ticks == 0) {
        ticks = 1;
    }
    vTaskDelay(ticks);
}

static void prg32_configure_metrics(const char *game_name) {
    snprintf(g_metrics_game_name,
             sizeof(g_metrics_game_name),
             "%s",
             game_name && game_name[0] ? game_name : "idle");
    g_metrics_config = (prg32_metrics_config_t){
        .enabled = CONFIG_PRG32_METRICS_ENABLE,
        .server_url = CONFIG_PRG32_METRICS_SERVER_URL,
        .board_id = CONFIG_PRG32_METRICS_BOARD_ID,
        .target = CONFIG_IDF_TARGET,
        .display_backend = PRG32_DISPLAY_BACKEND_NAME,
        .firmware_version = PRG32_FIRMWARE_VERSION,
        .firmware_git_sha = "unknown",
        .game_name = g_metrics_game_name,
        .sample_period_frames = CONFIG_PRG32_METRICS_SAMPLE_PERIOD_FRAMES,
        .upload_period_ms = CONFIG_PRG32_METRICS_UPLOAD_PERIOD_MS,
    };
    prg32_metrics_init(&g_metrics_config);
}

static void prg32_start_metrics_for_loaded_cart(void) {
    prg32_cart_info_t info;
    const char *game_name = "cartridge";
    if (prg32_cart_get_info(&info) == 0 && info.name[0]) {
        game_name = info.name;
    }
    prg32_configure_metrics(game_name);
    prg32_metrics_start_run();
    g_metrics_run_active = 1;
}

static void prg32_stop_metrics_run(void) {
    if (g_metrics_run_active) {
        prg32_metrics_stop_run();
        g_metrics_run_active = 0;
    }
}

static uint16_t prg32_metric_fps_x100(uint32_t frame_us) {
    if (frame_us == 0) {
        return 0;
    }
    uint32_t fps_x100 = (uint32_t)(100000000ULL / frame_us);
    return fps_x100 > UINT16_MAX ? UINT16_MAX : (uint16_t)fps_x100;
}

static void prg32_record_frame_metric(uint32_t frame,
                                      uint32_t input_snapshot,
                                      int64_t frame_start_us,
                                      int64_t update_start_us,
                                      int64_t update_end_us,
                                      int64_t draw_start_us,
                                      int64_t draw_end_us,
                                      int64_t present_start_us,
                                      int64_t present_end_us) {
    uint32_t update_us = (uint32_t)(update_end_us - update_start_us);
    uint32_t draw_us = (uint32_t)(draw_end_us - draw_start_us);
    uint32_t present_us = (uint32_t)(present_end_us - present_start_us);
    uint32_t frame_us = (uint32_t)(present_end_us - frame_start_us);
    prg32_metric_sample_t sample = {
        .frame = frame,
        .timestamp_ms = prg32_ticks_ms(),
        .update_us = update_us,
        .draw_us = draw_us,
        .present_us = present_us,
        .frame_us = frame_us,
        .heap_free = esp_get_free_heap_size(),
        .heap_min_free = esp_get_minimum_free_heap_size(),
        .input_mask = input_snapshot,
        .fps_x100 = prg32_metric_fps_x100(frame_us),
        .upload_queue_depth = 0,
        .deadline_missed = frame_us > (PRG32_FRAME_MS * 1000u),
        .reserved = 0,
    };
    prg32_metrics_record(&sample);
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

static void check_terminal_keyboard_input(void) {
    static int initialized = 0;
    if (!initialized) {
        if (!uart_is_driver_installed(UART_NUM_0)) {
            uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
        }
        initialized = 1;
    }

    uint32_t active_mask = 0;
    uint8_t c;
    while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
        switch (c) {
            case 'w': active_mask |= PRG32_BTN_UP; break;
            case 's': active_mask |= PRG32_BTN_DOWN; break;
            case 'a': active_mask |= PRG32_BTN_LEFT; break;
            case 'd': active_mask |= PRG32_BTN_RIGHT; break;
            case ' ': active_mask |= PRG32_BTN_A; break;
            case 'b': active_mask |= PRG32_BTN_B; break;
            case '\r':
            case '\n': active_mask |= PRG32_BTN_START; break;
        }
    }
    prg32_diag_set_input_state(active_mask);
}

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
        // check_terminal_keyboard_input();
        uint32_t input_snapshot = prg32_controller_read();

        if (prg32_cart_is_loaded()) {
            uint32_t current_generation = prg32_cart_generation();
            if (current_generation != cart_generation) {
                cart_generation = current_generation;
                prg32_console_clear();
                prg32_stop_metrics_run();
                prg32_cart_call_init();
                prg32_start_metrics_for_loaded_cart();
            }
            if (prg32_metrics_is_enabled()) {
                int64_t frame_start_us = esp_timer_get_time();
                prg32_gfx_lock();
                int64_t update_start_us = esp_timer_get_time();
                prg32_cart_call_update();
                int64_t update_end_us = esp_timer_get_time();
                int64_t draw_start_us = update_end_us;
                prg32_cart_call_draw();
                int64_t draw_end_us = esp_timer_get_time();
#if PRG32_DEBUG
                prg32_debug_overlay_draw(1, 0, 0, input_snapshot, prg32_diag_frame_count());
#endif
                int64_t present_start_us = esp_timer_get_time();
                prg32_gfx_present();
                int64_t present_end_us = esp_timer_get_time();
                prg32_gfx_unlock();
                prg32_record_frame_metric(prg32_diag_frame_count(),
                                          input_snapshot,
                                          frame_start_us,
                                          update_start_us,
                                          update_end_us,
                                          draw_start_us,
                                          draw_end_us,
                                          present_start_us,
                                          present_end_us);
            } else {
                prg32_gfx_lock();
                prg32_cart_call_update();
                prg32_cart_call_draw();
#if PRG32_DEBUG
                prg32_debug_overlay_draw(1, 0, 0, input_snapshot, prg32_diag_frame_count());
#endif
                prg32_gfx_present();
                prg32_gfx_unlock();
            }
            prg32_diag_increment_frame();
            prg32_wait_for_frame_target(&next_frame_ms);
        } else {
            prg32_stop_metrics_run();
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
