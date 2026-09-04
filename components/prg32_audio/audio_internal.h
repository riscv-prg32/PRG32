#ifndef PRG32_AUDIO_INTERNAL_H
#define PRG32_AUDIO_INTERNAL_H

#include "audio_config.h"
#include "prg32_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct {
    const uint8_t *data;
    prg32_sample_desc_t desc;
    bool present;
} prg32_audio_sample_slot_t;

typedef struct {
    prg32_instrument_desc_t desc;
    bool present;
} prg32_audio_instrument_slot_t;

typedef struct {
    const prg32_audio_event_t *events;
    uint32_t event_count;
    bool present;
} prg32_audio_track_slot_t;

typedef struct {
    uint32_t phase;
    uint32_t phase_increment;
    uint32_t pulse_threshold;
    uint32_t lfsr;
    int32_t filter_low;
    int32_t filter_band;
    uint32_t envelope_level;
    uint32_t envelope_step;
    uint32_t sustain_level;
    uint8_t waveform;
    uint8_t cutoff;
    uint8_t resonance;
    uint8_t envelope_state;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
} prg32_audio_synth_voice_t;

typedef struct {
    bool active;
    bool synth;
    uint16_t sample_id;
    uint32_t position_fp;
    uint32_t step_fp;
    uint8_t volume;
    int8_t pan;
    bool loop;
    uint32_t loop_start;
    uint32_t loop_end;
    prg32_audio_synth_voice_t synth_state;
} prg32_audio_voice_t;

typedef struct {
    bool active;
    uint16_t track_id;
    uint32_t event_index;
    uint32_t tick_accum;
    uint32_t next_delta;
    uint16_t tempo_bpm;
} prg32_audio_tracker_state_t;

typedef struct {
    bool initialized;
    bool i2s_ready;
    bool task_stop;
    prg32_audio_config_t config;
    uint8_t master_volume;
    uint8_t max_voices;
    uint8_t channel_volume[CONFIG_PRG32_AUDIO_MAX_VOICES];
    int8_t channel_pan[CONFIG_PRG32_AUDIO_MAX_VOICES];
    prg32_audio_voice_t voices[CONFIG_PRG32_AUDIO_MAX_VOICES];
    prg32_audio_sample_slot_t samples[PRG32_AUDIO_MAX_SAMPLES];
    prg32_audio_instrument_slot_t instruments[PRG32_AUDIO_MAX_INSTRUMENTS];
    prg32_audio_track_slot_t tracks[PRG32_AUDIO_MAX_TRACKS];
    prg32_audio_tracker_state_t tracker;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
} prg32_audio_state_t;

extern prg32_audio_state_t g_prg32_audio;

void prg32_audio_lock(void);
void prg32_audio_unlock(void);
void prg32_audio_tracker_step(uint32_t elapsed_ms);
void prg32_audio_synth_start(prg32_audio_voice_t *voice,
                             const prg32_instrument_desc_t *instrument,
                             uint8_t note, uint32_t sample_rate);
void prg32_audio_synth_release(prg32_audio_voice_t *voice,
                               uint32_t sample_rate);
int32_t prg32_audio_synth_next(prg32_audio_voice_t *voice,
                               uint32_t sample_rate);

int prg32_audio_i2s_start(const prg32_audio_config_t *config);
void prg32_audio_i2s_stop(void);
int prg32_audio_i2s_write(const int16_t *samples,
                          size_t frames,
                          prg32_audio_mode_t mode);

#endif
