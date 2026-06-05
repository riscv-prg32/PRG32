#ifndef PRG32_AUDIO_H
#define PRG32_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRG32_AUDIO_PAN_LEFT   (-64)
#define PRG32_AUDIO_PAN_CENTER 0
#define PRG32_AUDIO_PAN_RIGHT  63

#define PRG32_AUDIO_BLOCK_MAGIC "AUD0"
#define PRG32_AUDIO_BLOCK_VERSION 1

#define PRG32_AUDIO_SAMPLE_LOOP (1u << 0)

typedef enum {
    PRG32_AUDIO_MODE_MONO = 1,
    PRG32_AUDIO_MODE_STEREO = 2
} prg32_audio_mode_t;

typedef enum {
    PRG32_AUDIO_CMD_NOTE_ON = 1,
    PRG32_AUDIO_CMD_NOTE_OFF = 2,
    PRG32_AUDIO_CMD_SET_VOLUME = 3,
    PRG32_AUDIO_CMD_SET_PAN = 4,
    PRG32_AUDIO_CMD_SET_TEMPO = 5,
    PRG32_AUDIO_CMD_PLAY_SAMPLE = 6,
    PRG32_AUDIO_CMD_JUMP = 7,
    PRG32_AUDIO_CMD_END = 255,
} prg32_audio_command_t;

typedef struct {
    uint32_t sample_rate;
    prg32_audio_mode_t mode;
    uint8_t max_voices;
    int gpio_bclk;
    int gpio_lrclk;
    int gpio_data;
    int gpio_sd;
} prg32_audio_config_t;

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_end;
    uint16_t base_note;
    uint8_t flags;
    uint8_t reserved;
} prg32_sample_desc_t;

typedef struct {
    uint16_t sample_id;
    uint8_t default_volume;
    int8_t default_pan;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
} prg32_instrument_desc_t;

typedef struct {
    uint8_t delta_ticks;
    uint8_t command;
    uint8_t arg0;
    uint8_t arg1;
} prg32_audio_event_t;

typedef struct {
    uint32_t event_offset;
    uint32_t event_count;
} prg32_audio_track_desc_t;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t sample_count;
    uint16_t instrument_count;
    uint16_t track_count;
    uint16_t reserved;
    uint32_t samples_offset;
    uint32_t instruments_offset;
    uint32_t tracks_offset;
    uint32_t events_offset;
    uint32_t data_offset;
    uint32_t block_size;
} prg32_audio_block_header_t;

bool prg32_audio_init(const prg32_audio_config_t *config);
void prg32_audio_shutdown(void);
const char *prg32_audio_last_error(void);

prg32_audio_mode_t prg32_audio_get_mode(void);
int prg32_audio_is_ready(void);

int prg32_audio_register_sample(uint16_t sample_id,
                                const uint8_t *samples,
                                uint32_t length,
                                uint16_t base_note,
                                uint8_t flags,
                                uint32_t loop_start,
                                uint32_t loop_end);
int prg32_audio_register_instrument(uint16_t instrument_id,
                                    const prg32_instrument_desc_t *instrument);
int prg32_audio_register_track(uint16_t track_id,
                               const prg32_audio_event_t *events,
                               uint32_t event_count);
int prg32_audio_load_block(const void *block, size_t block_size);
void prg32_audio_clear_assets(void);

int prg32_audio_play_sample(uint16_t sample_id, uint8_t volume, uint16_t pitch);
int prg32_audio_play_sample_pan(uint16_t sample_id,
                                uint8_t volume,
                                uint16_t pitch,
                                int8_t pan);

void prg32_audio_stop_channel(int channel);
void prg32_audio_stop_all(void);

void prg32_audio_note_on(uint8_t channel,
                         uint8_t instrument,
                         uint8_t note,
                         uint8_t volume);
void prg32_audio_note_on_pan(uint8_t channel,
                             uint8_t instrument,
                             uint8_t note,
                             uint8_t volume,
                             int8_t pan);
void prg32_audio_note_off(uint8_t channel);

void prg32_audio_play_track(uint16_t track_id);
void prg32_audio_stop_track(void);
void prg32_audio_set_tempo(uint16_t bpm);

void prg32_audio_set_master_volume(uint8_t volume);
void prg32_audio_set_channel_volume(uint8_t channel, uint8_t volume);
void prg32_audio_set_channel_pan(uint8_t channel, int8_t pan);

void prg32_audio_mix_mono(int16_t *buffer, size_t frames);
void prg32_audio_mix_stereo(int16_t *buffer, size_t frames);
uint8_t prg32_audio_pan_left_gain(int8_t pan);
uint8_t prg32_audio_pan_right_gain(int8_t pan);

#ifdef __cplusplus
}
#endif

#endif
