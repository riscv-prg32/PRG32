#include "audio_internal.h"
#include "prg32_audio_frame_clock.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdbool.h>
#include <string.h>

#if PRG32_QEMU_AUDIO_REDIRECT
extern void prg32_qemu_audio_write_pcm(const int16_t *buffer, size_t frames,
                                       int mode);
extern bool prg32_qemu_audio_read_ack(uint8_t *ack, uint32_t timeout_ms);
extern void prg32_qemu_audio_init(void);
#endif

prg32_audio_state_t g_prg32_audio;

static uint8_t g_default_master_volume = 96;

void prg32_audio_set_default_master_volume(uint8_t volume) {
  g_default_master_volume = volume;
}

#if PRG32_QEMU_AUDIO_REDIRECT
/* The audio frame clock (prg32_audio_frame_clock.h). While a cartridge runs, its frames
 * tell the audio task how many 20 ms buffers to render (work_) and wait until it has
 * (done_); between frames the task renders nothing. render_lock_ is held while the task
 * mixes a buffer on its own, so a cartridge starts between two buffers. It is never held
 * while the task waits for the host's credit: that wait can be long or endless, and a
 * cartridge waiting for the lock would then never start. These live outside
 * g_prg32_audio because a cartridge may call prg32_audio_init, which clears that state. */
static SemaphoreHandle_t render_lock_, work_, done_;
static volatile bool cartridge_clock_;
static uint32_t cartridge_pending_ms_;

/* How long the task waits for one credit before it decides that no audio player is
 * connected (`qemu run --no-audio`, or QEMU started with `wait=off` and no player). */
#define PRG32_QEMU_AUDIO_HOST_WAIT_MS 500
static bool host_absent_;

static uint32_t buffer_ms(void) {
  return (uint32_t)(PRG32_AUDIO_MIX_FRAMES * 1000u /
                    g_prg32_audio.config.sample_rate);
}

/* Advance the engine by one buffer, mix it and send it to the host (QEMU UART1). */
static void mix_and_send_buffer(int16_t *buffer) {
  prg32_audio_tracker_step(buffer_ms());
  prg32_audio_voices_step(buffer_ms());
  if (prg32_audio_get_mode() == PRG32_AUDIO_MODE_STEREO) {
    prg32_audio_mix_stereo(buffer, PRG32_AUDIO_MIX_FRAMES);
  } else {
    prg32_audio_mix_mono(buffer, PRG32_AUDIO_MIX_FRAMES);
  }
  prg32_qemu_audio_write_pcm(buffer, PRG32_AUDIO_MIX_FRAMES,
                             prg32_audio_get_mode());
}

/* Wait for the host's 1-byte credit for the buffer just sent: QEMU fast-forwards when
 * idle and would otherwise generate audio faster than real time. With no player the
 * credit never comes, so after one PRG32_QEMU_AUDIO_HOST_WAIT_MS timeout the task stops
 * waiting and the cartridge keeps running silently. A player that connects later sends
 * its credits first, and the first one found turns the wait back on. */
static void wait_for_host_credit(void) {
  uint8_t ack;
  if (host_absent_) {
    if (prg32_qemu_audio_read_ack(&ack, 0)) {
      host_absent_ = false;
      ESP_LOGI("prg32_audio", "QEMU audio player connected");
    }
    return;
  }
  if (!prg32_qemu_audio_read_ack(&ack, PRG32_QEMU_AUDIO_HOST_WAIT_MS)) {
    host_absent_ = true;
    ESP_LOGW("prg32_audio", "no QEMU audio player: audio is not paced");
  }
}

/* One buffer under the audio frame clock: mix it, send it, wait for its credit. */
static void render_buffer(int16_t *buffer) {
  mix_and_send_buffer(buffer);
  wait_for_host_credit();
}

/* Cartridge time has moved on by `ms`: the audio task renders the whole buffers that
 * fit, and the cartridge waits for them. */
static void cartridge_advance(uint32_t ms) {
  if (!g_prg32_audio.task || !g_prg32_audio.i2s_ready || !work_) {
    return;
  }
  cartridge_pending_ms_ += ms;
  uint32_t buffers = 0;
  while (cartridge_pending_ms_ >= buffer_ms()) {
    cartridge_pending_ms_ -= buffer_ms();
    xSemaphoreGive(work_);
    ++buffers;
  }
  while (buffers-- > 0) {
    xSemaphoreTake(done_, portMAX_DELAY);
  }
}
#endif

void prg32_audio_cartridge_start(void) {
#if PRG32_QEMU_AUDIO_REDIRECT
  if (!render_lock_) {
    return;
  }
  // Wait for a buffer the audio task is rendering on its own; after that it renders
  // only the buffers the cartridge's frames ask for.
  xSemaphoreTake(render_lock_, portMAX_DELAY);
  cartridge_clock_ = true;
  cartridge_pending_ms_ = 0;
  xSemaphoreGive(render_lock_);
#endif
}

void prg32_audio_cartridge_frame(uint32_t ms) {
#if PRG32_QEMU_AUDIO_REDIRECT
  if (cartridge_clock_) {
    cartridge_advance(ms);
  }
#else
  (void)ms;
#endif
}

void prg32_audio_cartridge_stop(void) {
#if PRG32_QEMU_AUDIO_REDIRECT
  cartridge_clock_ = false;
#endif
}

/* prg32_audio_notes waits between its notes. Under the audio frame clock that wait is
 * rendered, as the browser core does; otherwise the task sleeps. */
void prg32_audio_wait_ms(uint32_t ms) {
#if PRG32_QEMU_AUDIO_REDIRECT
  if (cartridge_clock_) {
    cartridge_advance(ms);
    return;
  }
#endif
  vTaskDelay(pdMS_TO_TICKS(ms));
}

/* Given by the audio task just before it deletes itself. Created once and never
 * freed, because prg32_audio_shutdown() clears g_prg32_audio after taking it. */
static SemaphoreHandle_t task_exited_;

static void audio_task(void *arg) {
  (void)arg;
  int16_t buffer[PRG32_AUDIO_MIX_FRAMES * 2];
#if PRG32_QEMU_AUDIO_REDIRECT
  /* In QEMU nothing paces the task like the console's I2S DMA does, so the engine
   * advances by the audio it renders (one buffer per period), and the period is
   * kept on the emulated clock with vTaskDelayUntil. While a cartridge runs, its
   * frames ask for the buffers instead (the audio frame clock). */
  TickType_t next_wake = xTaskGetTickCount();
  // At least one tick: pdMS_TO_TICKS(4) is 0 at 100 Hz, and a task that never blocks
  // would starve the cartridge's lower-priority task.
  const TickType_t idle_ticks = pdMS_TO_TICKS(4) > 0 ? pdMS_TO_TICKS(4) : 1;

  while (!g_prg32_audio.task_stop) {
    if (cartridge_clock_) {
      if (xSemaphoreTake(work_, idle_ticks) == pdTRUE) {
        render_buffer(buffer);
        xSemaphoreGive(done_);
      }
      next_wake = xTaskGetTickCount();
      continue;
    }
    if (!g_prg32_audio.i2s_ready) {
      vTaskDelay(idle_ticks);
      next_wake = xTaskGetTickCount();
      continue;
    }
    bool sent = false;
    xSemaphoreTake(render_lock_, portMAX_DELAY);
    if (!cartridge_clock_) {
      mix_and_send_buffer(buffer);
      sent = true;
    }
    xSemaphoreGive(render_lock_);
    // Outside the lock, so that a cartridge can start while the host plays.
    if (sent) {
      wait_for_host_credit();
    }
    // Wait for the rest of this buffer's 20 ms on the emulated clock (441
    // frames at 22050 Hz), however long the acknowledgement took. When the
    // credit took longer than that, start the period again from now instead of
    // trying to catch up: rendering back to back would never block, and the
    // lower-priority main task would not run.
    if (xTaskDelayUntil(&next_wake, pdMS_TO_TICKS(buffer_ms())) == pdFALSE) {
      next_wake = xTaskGetTickCount();
    }
  }
#else
  int64_t last_us = esp_timer_get_time();

  while (!g_prg32_audio.task_stop) {
    int64_t now_us = esp_timer_get_time();
    uint32_t elapsed_ms = (uint32_t)((now_us - last_us) / 1000);
    if (elapsed_ms > 0) {
      last_us = now_us;
      prg32_audio_tracker_step(elapsed_ms);

      prg32_audio_voices_step(elapsed_ms);
    }

    if (prg32_audio_get_mode() == PRG32_AUDIO_MODE_STEREO) {
      prg32_audio_mix_stereo(buffer, PRG32_AUDIO_MIX_FRAMES);
    } else {
      prg32_audio_mix_mono(buffer, PRG32_AUDIO_MIX_FRAMES);
    }

    if (g_prg32_audio.i2s_ready) {
      prg32_audio_i2s_write(buffer, PRG32_AUDIO_MIX_FRAMES,
                            g_prg32_audio.config.mode);
    } else {
      vTaskDelay(pdMS_TO_TICKS(4));
    }
  }
#endif

  /* The last thing this task does: prg32_audio_shutdown() frees the lock and
   * clears g_prg32_audio as soon as it takes this semaphore. */
  xSemaphoreGive(task_exited_);
  vTaskDelete(NULL);
}

static prg32_audio_config_t default_config(void) {
  prg32_audio_config_t config = {
      .sample_rate = CONFIG_PRG32_AUDIO_SAMPLE_RATE,
      .mode = PRG32_AUDIO_DEFAULT_MODE,
      .max_voices = CONFIG_PRG32_AUDIO_MAX_VOICES,
      .gpio_bclk = CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO,
      .gpio_lrclk = CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO,
      .gpio_data = CONFIG_PRG32_AUDIO_I2S_DATA_GPIO,
      .gpio_sd = CONFIG_PRG32_AUDIO_I2S_SD_GPIO,
  };
  return config;
}

void prg32_audio_lock(void) {
  if (g_prg32_audio.lock) {
    xSemaphoreTake(g_prg32_audio.lock, portMAX_DELAY);
  }
}

void prg32_audio_unlock(void) {
  if (g_prg32_audio.lock) {
    xSemaphoreGive(g_prg32_audio.lock);
  }
}

// TODO: This could be turned into a dynamically generated
// PRG32_AUDIO_SYNTH_PULSE instead of being hard-saved.
void prg32_audio_restore_defaults(void) {
  static const uint8_t default_wave[] = {
      128, 166, 202, 231, 250, 255, 246, 224, 192, 154, 114, 76,  44,  20,  6,
      0,   6,   20,  44,  76,  114, 154, 192, 224, 246, 255, 250, 231, 202, 166,
  };

  prg32_audio_register_sample(62, default_wave, sizeof(default_wave), 60,
                              PRG32_AUDIO_SAMPLE_LOOP, 0, sizeof(default_wave));

  prg32_instrument_desc_t default_inst = {
      .sample_id = 62,
      .default_volume = 150,
      .default_pan = PRG32_AUDIO_PAN_CENTER,
      .sustain = 255,
  };
  prg32_audio_register_instrument(PRG32_DEFAULT_INSTRUMENT_ID, &default_inst);
  prg32_audio_register_instrument(31, &default_inst);
}

bool prg32_audio_init(const prg32_audio_config_t *config) {
#if !CONFIG_PRG32_AUDIO_ENABLED
  (void)config;
  return false;
#else
  prg32_audio_config_t chosen = config ? *config : default_config();
  if (chosen.sample_rate == 0) {
    chosen.sample_rate = CONFIG_PRG32_AUDIO_SAMPLE_RATE;
  }
  if (chosen.mode != PRG32_AUDIO_MODE_STEREO) {
    chosen.mode = PRG32_AUDIO_MODE_MONO;
  }
  if (chosen.max_voices == 0 ||
      chosen.max_voices > CONFIG_PRG32_AUDIO_MAX_VOICES) {
    chosen.max_voices = CONFIG_PRG32_AUDIO_MAX_VOICES;
  }

  if (!g_prg32_audio.lock) {
    g_prg32_audio.lock = xSemaphoreCreateMutex();
  }

  prg32_audio_shutdown();
  memset(&g_prg32_audio, 0, sizeof(g_prg32_audio));
  g_prg32_audio.lock = xSemaphoreCreateMutex();
  g_prg32_audio.config = chosen;
  g_prg32_audio.master_volume = g_default_master_volume;
  g_prg32_audio.max_voices = chosen.max_voices;
  g_prg32_audio.tracker.tempo_bpm = 120;
  for (uint8_t i = 0; i < CONFIG_PRG32_AUDIO_MAX_VOICES; ++i) {
    g_prg32_audio.channel_volume[i] = 255;
    g_prg32_audio.channel_pan[i] = PRG32_AUDIO_PAN_CENTER;
  }

#if PRG32_QEMU_AUDIO_REDIRECT
  prg32_qemu_audio_init();
  g_prg32_audio.i2s_ready = true;
#else
  g_prg32_audio.i2s_ready = prg32_audio_i2s_start(&chosen) == 0;
#endif
  g_prg32_audio.initialized = true;

  prg32_audio_restore_defaults();

#if PRG32_QEMU_AUDIO_REDIRECT
  if (!render_lock_) {
    render_lock_ = xSemaphoreCreateMutex();
    work_ = xSemaphoreCreateCounting(1000, 0);
    done_ = xSemaphoreCreateCounting(1000, 0);
  }
#endif
  /* Above the tasks that can lend their priority to the main loop: the HTTP
   * server (tskIDLE_PRIORITY + 5) and the device protocol task (5) wait for
   * the cartridge and graphics locks that the main loop holds for a whole
   * frame, and priority inheritance then runs that frame at their priority.
   * At + 2 the audio task got no CPU for the rest of the frame (80-220 ms
   * measured with /api/runtime and /api/screenshot.bmp requests) and the
   * 23 ms I2S DMA queue ran dry: the speaker buzzed while frames slowed.
   * Below lwIP and Wi-Fi. The task blocks on every buffer (I2S DMA on ESP32-C6,
   * the host's credit or the frame clock on QEMU), so it cannot starve them. */
  if (!task_exited_) {
    task_exited_ = xSemaphoreCreateBinary();
  }
#if PRG32_QEMU_AUDIO_REDIRECT
  // Frame-clock requests left over from the previous task belong to it.
  while (xSemaphoreTake(work_, 0) == pdTRUE) {
  }
  while (xSemaphoreTake(done_, 0) == pdTRUE) {
  }
#endif
  int priority = tskIDLE_PRIORITY + 6;
  xTaskCreate(audio_task, "prg32_audio", 4096, NULL, priority,
              &g_prg32_audio.task);
  return g_prg32_audio.i2s_ready;
#endif
}

/* Stop the audio task and wait until it has really exited, without a time
 * limit: every wait in the task is bounded (one I2S DMA buffer on ESP32-C6; the
 * frame clock's 4 ms, the 20 ms period or the host's 500 ms credit on QEMU).
 * A shorter cap let a task that was waiting for the QEMU audio player's credit
 * outlive the wait. The lock was then freed under it, the memset below cleared
 * its stop flag, and the old and the new task ran together: when the old one
 * finally exited it cleared the new task's handle, and the kernel's lists were
 * corrupted (QEMU hung in vListInsert while starting a cartridge). */
void prg32_audio_shutdown(void) {
  if (g_prg32_audio.task) {
    g_prg32_audio.task_stop = true;
    xSemaphoreTake(task_exited_, portMAX_DELAY);
    g_prg32_audio.task = NULL;
  }
  prg32_audio_i2s_stop();
  if (g_prg32_audio.lock) {
    vSemaphoreDelete(g_prg32_audio.lock);
  }
  memset(&g_prg32_audio, 0, sizeof(g_prg32_audio));
}

prg32_audio_mode_t prg32_audio_get_mode(void) {
  if (!g_prg32_audio.initialized) {
    return PRG32_AUDIO_DEFAULT_MODE;
  }
  return g_prg32_audio.config.mode;
}

void prg32_audio_set_mode(prg32_audio_mode_t mode) {
  if (mode != PRG32_AUDIO_MODE_MONO && mode != PRG32_AUDIO_MODE_STEREO) {
    return;
  }
  prg32_audio_lock();
  g_prg32_audio.config.mode = mode;
  prg32_audio_unlock();
}

int prg32_audio_is_ready(void) { return g_prg32_audio.initialized ? 1 : 0; }

void prg32_audio_set_master_volume(uint8_t volume) {
  prg32_audio_lock();
  g_prg32_audio.master_volume = volume;
  prg32_audio_unlock();
}

void prg32_audio_set_channel_volume(uint8_t channel, uint8_t volume) {
  if (channel >= CONFIG_PRG32_AUDIO_MAX_VOICES) {
    return;
  }
  prg32_audio_lock();
  g_prg32_audio.channel_volume[channel] = volume;
  prg32_audio_unlock();
}

void prg32_audio_set_channel_pan(uint8_t channel, int8_t pan) {
  if (channel >= CONFIG_PRG32_AUDIO_MAX_VOICES) {
    return;
  }
  if (pan < PRG32_AUDIO_PAN_LEFT) {
    pan = PRG32_AUDIO_PAN_LEFT;
  }
  if (pan > PRG32_AUDIO_PAN_RIGHT) {
    pan = PRG32_AUDIO_PAN_RIGHT;
  }
  prg32_audio_lock();
  g_prg32_audio.channel_pan[channel] = pan;
  if (channel < g_prg32_audio.max_voices) {
    g_prg32_audio.voices[channel].pan = pan;
  }
  prg32_audio_unlock();
}
