#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prg32.h"
#include "prg32_config.h"
#include <math.h>

#ifdef __ELF__
#define PRG32_FLASH_RODATA __attribute__((section(".rodata")))
#else
#define PRG32_FLASH_RODATA
#endif

void prg32_buzzer_init(void) {
  if (PRG32_PIN_BUZZER < 0) {
    return;
  }
  ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                               .timer_num = LEDC_TIMER_0,
                               .duty_resolution = LEDC_TIMER_10_BIT,
                               .freq_hz = 1000,
                               .clk_cfg = LEDC_AUTO_CLK};
  ledc_channel_config_t ch = {.gpio_num = PRG32_PIN_BUZZER,
                              .speed_mode = LEDC_LOW_SPEED_MODE,
                              .channel = LEDC_CHANNEL_0,
                              .timer_sel = LEDC_TIMER_0,
                              .duty = 0,
                              .hpoint = 0};
  ledc_timer_config(&timer);
  ledc_channel_config(&ch);
}

void prg32_buzzer_tone(uint32_t hz, uint32_t ms, uint16_t duty) {
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
  if (!hz || !ms || !prg32_audio_is_ready()) {
    return;
  }
  if (duty > 1023) {
    duty = 1023;
  }
  int note = (int)lroundf(69.0f + 12.0f * log2f((float)hz / 440.0f));
  if (note < 0) {
    note = 0;
  } else if (note > 127) {
    note = 127;
  }
  uint8_t volume = (uint8_t)((duty * 255u + 511u) / 1023u);
  prg32_audio_note(0, PRG32_DEFAULT_INSTRUMENT_ID, (uint8_t)note, volume, ms);
  vTaskDelay(pdMS_TO_TICKS(ms));
  prg32_audio_note_off(0);
  return;
#else
  if (PRG32_PIN_BUZZER < 0) {
    return;
  }
  if (!hz || !ms) {
    return;
  }
  if (duty > 1023) {
    duty = 1023;
  }
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, hz);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  vTaskDelay(pdMS_TO_TICKS(ms));
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

void prg32_buzzer_play_notes(const prg32_note_t *notes, size_t count) {
  if (!notes) {
    return;
  }
#if !CONFIG_PRG32_DISPLAY_QEMU_RGB
  if (PRG32_PIN_BUZZER < 0) {
    return;
  }
#endif
  for (size_t i = 0; i < count; ++i) {
    if (notes[i].frequency_hz == 0) {
      vTaskDelay(pdMS_TO_TICKS(notes[i].duration_ms));
    } else {
      prg32_buzzer_tone(notes[i].frequency_hz, notes[i].duration_ms, 512);
    }
  }
}

void prg32_buzzer_sample_u8(const uint8_t *samples, size_t count,
                            uint32_t sample_rate) {
  if (PRG32_PIN_BUZZER < 0 || !samples || count == 0 || sample_rate == 0) {
    return;
  }
  uint32_t delay_us = 1000000u / sample_rate;
  if (delay_us == 0) {
    delay_us = 1;
  }
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, sample_rate);
  for (size_t i = 0; i < count; ++i) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, samples[i] * 4u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    esp_rom_delay_us(delay_us);
  }
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
