#include "prg32.h"
#include "prg32_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const uint8_t tick[] = {
    128, 255, 240, 200, 160, 120, 90, 70,
    90, 120, 128,
};

void app_main(void) {
    prg32_init();
    prg32_console_write("PRG32 stereo pan test\n");

    prg32_audio_config_t config = {
        .sample_rate = 22050,
        .mode = PRG32_AUDIO_MODE_STEREO,
        .max_voices = 8,
        .gpio_bclk = 4,
        .gpio_lrclk = 11,
        .gpio_data = 23,
        .gpio_sd = -1,
    };
    prg32_audio_init(&config);
    prg32_audio_register_sample(0, tick, sizeof(tick), 60, 0, 0, 0);

    while (1) {
        prg32_console_write("left\n");
        prg32_audio_play_sample_pan(0, 255, 1024, PRG32_AUDIO_PAN_LEFT);
        vTaskDelay(pdMS_TO_TICKS(700));
        prg32_console_write("right\n");
        prg32_audio_play_sample_pan(0, 255, 1024, PRG32_AUDIO_PAN_RIGHT);
        vTaskDelay(pdMS_TO_TICKS(700));
        prg32_console_write("center\n");
        prg32_audio_play_sample_pan(0, 255, 1024, PRG32_AUDIO_PAN_CENTER);
        vTaskDelay(pdMS_TO_TICKS(900));
    }
}
