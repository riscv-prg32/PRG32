#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prg32.h"
#include "prg32_config.h"

void prg32_audio_pwm_init(void) {
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

void prg32_audio_beep(uint32_t hz, uint32_t ms) {
  prg32_audio_tone(hz, ms, 512);
}

#if 0
// Kept for reference (square wave / "buzzer")
static const uint8_t builtin_beep_wave[32] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};
#endif

static const uint8_t setup_audio_wave[] = {
    128, 176, 218, 245, 255, 245, 218, 176, 128, 80, 38, 11, 1, 11, 38, 80,
    128, 176, 218, 245, 255, 245, 218, 176, 128, 80, 38, 11, 1, 11, 38, 80,
};

static uint8_t hz_to_midi_note(uint32_t hz) {
  if (hz < 16)
    return 0;
  uint8_t best_note = 60;
  uint32_t best_diff = 999999;
  for (uint8_t note = 12; note < 120; ++note) {
    static const uint16_t octave4[12] = {
        262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
    };
    uint32_t freq = octave4[note % 12u];
    int octave = (int)(note / 12u) - 1;
    while (octave > 4) {
      freq *= 2u;
      octave--;
    }
    while (octave < 4 && freq > 1u) {
      freq /= 2u;
      octave++;
    }
    uint32_t diff = (hz > freq) ? (hz - freq) : (freq - hz);
    if (diff < best_diff) {
      best_diff = diff;
      best_note = note;
    }
  }
  return best_note;
}

void prg32_audio_tone(uint32_t hz, uint32_t ms, uint16_t duty) {
  if (PRG32_PIN_BUZZER < 0) {
#if CONFIG_PRG32_AUDIO_ENABLED
    if (prg32_audio_is_ready() && hz > 0 && ms > 0) {
      prg32_instrument_desc_t inst = {
          .sample_id = 63,
          .default_volume = 192,
          .default_pan = PRG32_AUDIO_PAN_CENTER,
          .attack = 0,
          .decay = 0,
          .sustain = 255,
          .release = 0,
      };
      prg32_audio_register_sample(
          63, setup_audio_wave, sizeof(setup_audio_wave), 60,
          PRG32_AUDIO_SAMPLE_LOOP, 0, sizeof(setup_audio_wave));
      prg32_audio_register_instrument(31, &inst);

      uint8_t midi_note = hz_to_midi_note(hz);
      uint8_t vol = (duty > 1023) ? 255 : (uint8_t)(duty / 4u);
      prg32_audio_led_vu_level(vol);
      prg32_audio_note_on(0, 31, midi_note, vol);
      vTaskDelay(pdMS_TO_TICKS(ms));
      prg32_audio_note_off(0);
      prg32_audio_led_vu_level(0);
    } else if (ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(ms));
    }
#else
    if (ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(ms));
    }
#endif
    return;
  }
  if (!hz || !ms) {
    return;
  }
  if (duty > 1023) {
    duty = 1023;
  }
  prg32_audio_led_vu_level((uint8_t)(duty / 4u));
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, hz);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  vTaskDelay(pdMS_TO_TICKS(ms));
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  prg32_audio_led_vu_level(0);
}

void prg32_audio_note(uint8_t midi_note, uint32_t ms) {
  static const uint16_t octave4[12] = {
      262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
  };
  uint32_t freq = octave4[midi_note % 12u];
  int octave = (int)(midi_note / 12u) - 1;
  while (octave > 4) {
    freq *= 2u;
    octave--;
  }
  while (octave < 4 && freq > 1u) {
    freq /= 2u;
    octave++;
  }
  prg32_audio_tone(freq, ms, 512);
}

void prg32_audio_play_notes(const prg32_note_t *notes, size_t count) {
  if (!notes) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    if (notes[i].frequency_hz == 0) {
      vTaskDelay(pdMS_TO_TICKS(notes[i].duration_ms));
    } else {
      prg32_audio_tone(notes[i].frequency_hz, notes[i].duration_ms, 512);
    }
  }
}

void prg32_audio_sample_u8(const uint8_t *samples, size_t count,
                           uint32_t sample_rate) {
  if (PRG32_PIN_BUZZER < 0 || !samples || count == 0 || sample_rate == 0) {
    if (PRG32_PIN_BUZZER < 0 && sample_rate > 0 && count > 0) {
      uint32_t ms = (count * 1000u) / sample_rate;
      if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
      }
    }
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
