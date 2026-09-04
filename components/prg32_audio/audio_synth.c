#include "audio_internal.h"

#define SYNTH_ENV_OFF 0u
#define SYNTH_ENV_ATTACK 1u
#define SYNTH_ENV_DECAY 2u
#define SYNTH_ENV_SUSTAIN 3u
#define SYNTH_ENV_RELEASE 4u
#define SYNTH_ENV_MAX 65535u
#define SYNTH_LFSR_MASK 0x7fffffu
#define SYNTH_LFSR_SEED 0x7ffff8u
#define SYNTH_FILTER_LIMIT (1 << 22)

static int32_t clamp_filter(int64_t value) {
    if (value > SYNTH_FILTER_LIMIT) return SYNTH_FILTER_LIMIT;
    if (value < -SYNTH_FILTER_LIMIT) return -SYNTH_FILTER_LIMIT;
    return (int32_t)value;
}

/* Byte 1 is about 1 ms; byte 255 is about 2 seconds. */
static uint32_t envelope_samples(uint8_t value, uint32_t sample_rate) {
    if (value == 0 || sample_rate == 0) return 0;
    uint32_t milliseconds = 1u + ((uint32_t)value * value * 2000u) / 65025u;
    uint64_t samples = ((uint64_t)sample_rate * milliseconds) / 1000u;
    return samples ? (uint32_t)samples : 1u;
}

static uint32_t rising_step(uint32_t from, uint32_t to, uint32_t samples) {
    if (samples == 0 || from >= to) return to - from;
    uint32_t distance = to - from;
    return (distance + samples - 1u) / samples;
}

static uint32_t falling_step(uint32_t from, uint32_t to, uint32_t samples) {
    if (samples == 0 || from <= to) return from - to;
    uint32_t distance = from - to;
    return (distance + samples - 1u) / samples;
}

static uint32_t note_phase_increment(uint8_t note, uint32_t sample_rate) {
    static const uint32_t midi0_semitones_q16[12] = {
        535808, 567673, 601430, 637194, 675084, 715221,
        757750, 802808, 850542, 901120, 954692, 1011457,
    };
    if (sample_rate == 0) return 0;
    uint32_t frequency_q16 = midi0_semitones_q16[note % 12u];
    uint8_t octave = note / 12u;
    uint64_t scaled = (uint64_t)frequency_q16 << octave;
    if (scaled > UINT32_MAX) scaled = UINT32_MAX;
    uint64_t increment = (scaled << 16) / sample_rate;
    return increment > UINT32_MAX ? UINT32_MAX : (uint32_t)increment;
}

static void begin_decay(prg32_audio_synth_voice_t *synth,
                        uint32_t sample_rate) {
    synth->envelope_state = SYNTH_ENV_DECAY;
    synth->envelope_step = falling_step(
        synth->envelope_level, synth->sustain_level,
        envelope_samples(synth->decay, sample_rate));
    if (synth->envelope_level <= synth->sustain_level ||
        synth->envelope_step == 0) {
        synth->envelope_level = synth->sustain_level;
        synth->envelope_state = SYNTH_ENV_SUSTAIN;
    }
}

void prg32_audio_synth_start(prg32_audio_voice_t *voice,
                             const prg32_instrument_desc_t *instrument,
                             uint8_t note, uint32_t sample_rate) {
    prg32_audio_synth_voice_t *synth = &voice->synth_state;
    uint16_t id = instrument->sample_id;
    synth->waveform = id & 0x03u;
    synth->cutoff = (id >> 6) & 0x0fu;
    synth->resonance = (id >> 10) & 0x03u;
    uint32_t pulse_position = ((id >> 2) & 0x0fu) + 1u;
    synth->pulse_threshold = (uint32_t)(((uint64_t)pulse_position << 32) / 17u);
    synth->phase_increment = note_phase_increment(note, sample_rate);
    synth->lfsr = SYNTH_LFSR_SEED;
    synth->attack = instrument->attack;
    synth->decay = instrument->decay;
    synth->sustain = instrument->sustain;
    synth->release = instrument->release;
    synth->sustain_level = (uint32_t)instrument->sustain * 257u;
    synth->envelope_state = SYNTH_ENV_ATTACK;
    synth->envelope_step = rising_step(
        0, SYNTH_ENV_MAX, envelope_samples(synth->attack, sample_rate));
    if (synth->attack == 0) {
        synth->envelope_level = SYNTH_ENV_MAX;
        begin_decay(synth, sample_rate);
    }
}

void prg32_audio_synth_release(prg32_audio_voice_t *voice,
                               uint32_t sample_rate) {
    prg32_audio_synth_voice_t *synth = &voice->synth_state;
    if (!voice->active || !voice->synth ||
        synth->envelope_state == SYNTH_ENV_RELEASE) return;
    uint32_t samples = envelope_samples(synth->release, sample_rate);
    if (samples == 0 || synth->envelope_level == 0) {
        synth->envelope_level = 0;
        synth->envelope_state = SYNTH_ENV_OFF;
        voice->active = false;
        return;
    }
    synth->envelope_state = SYNTH_ENV_RELEASE;
    synth->envelope_step = falling_step(synth->envelope_level, 0, samples);
}

static void advance_envelope(prg32_audio_voice_t *voice,
                             uint32_t sample_rate) {
    prg32_audio_synth_voice_t *synth = &voice->synth_state;
    switch (synth->envelope_state) {
    case SYNTH_ENV_ATTACK:
        if (synth->envelope_level >= SYNTH_ENV_MAX - synth->envelope_step) {
            synth->envelope_level = SYNTH_ENV_MAX;
            begin_decay(synth, sample_rate);
        } else {
            synth->envelope_level += synth->envelope_step;
        }
        break;
    case SYNTH_ENV_DECAY:
        if (synth->envelope_level <= synth->sustain_level + synth->envelope_step) {
            synth->envelope_level = synth->sustain_level;
            synth->envelope_state = SYNTH_ENV_SUSTAIN;
        } else {
            synth->envelope_level -= synth->envelope_step;
        }
        break;
    case SYNTH_ENV_RELEASE:
        if (synth->envelope_level <= synth->envelope_step) {
            synth->envelope_level = 0;
            synth->envelope_state = SYNTH_ENV_OFF;
            voice->active = false;
        } else {
            synth->envelope_level -= synth->envelope_step;
        }
        break;
    default:
        break;
    }
}

static int32_t oscillator(prg32_audio_synth_voice_t *synth) {
    uint32_t old_phase = synth->phase;
    synth->phase += synth->phase_increment;
    if (synth->waveform == PRG32_AUDIO_SYNTH_WAVE_NOISE &&
        synth->phase < old_phase) {
        uint32_t feedback = ((synth->lfsr >> 22) ^ (synth->lfsr >> 17)) & 1u;
        synth->lfsr = ((synth->lfsr << 1) & SYNTH_LFSR_MASK) | feedback;
        if (synth->lfsr == 0) synth->lfsr = SYNTH_LFSR_SEED;
    }
    switch (synth->waveform) {
    case PRG32_AUDIO_SYNTH_WAVE_TRIANGLE: {
        uint32_t ramp = synth->phase & 0x7fffffffu;
        if (synth->phase & 0x80000000u) ramp = 0x7fffffffu - ramp;
        return (int32_t)(ramp >> 15) - 32768;
    }
    case PRG32_AUDIO_SYNTH_WAVE_SAW:
        return (int32_t)(synth->phase >> 16) - 32768;
    case PRG32_AUDIO_SYNTH_WAVE_PULSE:
        return synth->phase < synth->pulse_threshold ? 32767 : -32768;
    default:
        return (int32_t)((synth->lfsr >> 7) & 0xffffu) - 32768;
    }
}

static int32_t filter_sample(prg32_audio_synth_voice_t *synth, int32_t input) {
    static const uint16_t cutoff_q15[16] = {
        384, 512, 704, 960, 1280, 1696, 2208, 2848,
        3616, 4512, 5536, 6688, 7936, 9280, 10752, 12288,
    };
    static const uint16_t damping_q15[4] = {32767, 24576, 16384, 8192};
    int32_t low = synth->filter_low;
    int32_t band = synth->filter_band;
    low = clamp_filter((int64_t)low +
                       (((int64_t)cutoff_q15[synth->cutoff] * band) >> 15));
    int32_t high = clamp_filter((int64_t)input - low -
        (((int64_t)damping_q15[synth->resonance] * band) >> 15));
    band = clamp_filter((int64_t)band +
                        (((int64_t)cutoff_q15[synth->cutoff] * high) >> 15));
    synth->filter_low = low;
    synth->filter_band = band;
    if (low > 32767) return 32767;
    if (low < -32768) return -32768;
    return low;
}

int32_t prg32_audio_synth_next(prg32_audio_voice_t *voice,
                               uint32_t sample_rate) {
    prg32_audio_synth_voice_t *synth = &voice->synth_state;
    int32_t sample = oscillator(synth);
    advance_envelope(voice, sample_rate);
    sample = (int32_t)(((int64_t)sample * synth->envelope_level) >> 16);
    return filter_sample(synth, sample);
}
