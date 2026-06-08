#include "prg32.h"
#include "prg32_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const uint8_t lead_wave[] = {
    128, 180, 225, 245, 225, 180, 128, 76,
    31, 11, 31, 76,
};

static const uint8_t sfx_wave[] = {
    128, 255, 128, 64, 128,
};

static const prg32_audio_event_t song[] = {
    {0, PRG32_AUDIO_CMD_NOTE_ON, 0, 60},
    {4, PRG32_AUDIO_CMD_NOTE_OFF, 0, 0},
    {0, PRG32_AUDIO_CMD_NOTE_ON, 0, 67},
    {4, PRG32_AUDIO_CMD_NOTE_OFF, 0, 0},
    {0, PRG32_AUDIO_CMD_JUMP, 0, 0},
};

void app_main(void) {
    prg32_init();
    prg32_console_write("PRG32 stereo music\n");

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
    prg32_audio_register_sample(0, lead_wave, sizeof(lead_wave), 60, PRG32_AUDIO_SAMPLE_LOOP, 0, sizeof(lead_wave));
    prg32_audio_register_sample(1, sfx_wave, sizeof(sfx_wave), 60, 0, 0, 0);

    prg32_instrument_desc_t lead = {
        .sample_id = 0,
        .default_volume = 130,
        .default_pan = 0,
        .sustain = 255,
    };
    prg32_audio_register_instrument(0, &lead);
    prg32_audio_register_track(0, song, sizeof(song) / sizeof(song[0]));
    prg32_audio_play_track(0);

    while (1) {
        prg32_audio_play_sample_pan(1, 220, 1024, PRG32_AUDIO_PAN_LEFT);
        vTaskDelay(pdMS_TO_TICKS(600));
        prg32_audio_play_sample_pan(1, 220, 1024, PRG32_AUDIO_PAN_RIGHT);
        vTaskDelay(pdMS_TO_TICKS(600));
    }
}
