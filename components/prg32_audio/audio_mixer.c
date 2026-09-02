#include "audio_internal.h"
#include <string.h>

static int16_t clamp16(int32_t value) {
  if (value > 32767) {
    return 32767;
  }
  if (value < -32768) {
    return -32768;
  }
  return (int16_t)value;
}

static uint16_t note_pitch(uint8_t note, uint16_t base_note) {
  static const uint16_t ratio_q10[12] = {
      1024, 1085, 1149, 1218, 1290, 1367, 1448, 1534, 1625, 1722, 1825, 1933,
  };
  int delta = (int)note - (int)base_note;
  int octave = delta / 12;
  int semitone = delta % 12;
  if (semitone < 0) {
    semitone += 12;
    octave -= 1;
  }
  uint32_t pitch = ratio_q10[semitone];
  while (octave > 0) {
    pitch <<= 1;
    octave--;
  }
  while (octave < 0 && pitch > 1) {
    pitch >>= 1;
    octave++;
  }
  if (pitch > 65535) {
    pitch = 65535;
  }
  return (uint16_t)pitch;
}

static uint32_t pitch_to_step(uint16_t pitch) {
  if (pitch == 0) {
    pitch = PRG32_AUDIO_PITCH_BASE;
  }
  return ((uint32_t)pitch << PRG32_AUDIO_FP_SHIFT) / PRG32_AUDIO_PITCH_BASE;
}

static int alloc_voice(void) {
  for (uint8_t i = 0; i < g_prg32_audio.max_voices; ++i) {
    if (!g_prg32_audio.voices[i].active) {
      return i;
    }
  }
  return g_prg32_audio.max_voices > 0 ? 0 : -1;
}

static int start_voice(int channel, uint16_t sample_id, uint8_t volume,
                       uint16_t pitch, int8_t pan) {
  if (!g_prg32_audio.initialized || sample_id >= PRG32_AUDIO_MAX_SAMPLES ||
      !g_prg32_audio.samples[sample_id].present) {
    return -1;
  }
  if (pan < PRG32_AUDIO_PAN_LEFT) {
    pan = PRG32_AUDIO_PAN_LEFT;
  }
  if (pan > PRG32_AUDIO_PAN_RIGHT) {
    pan = PRG32_AUDIO_PAN_RIGHT;
  }

  int chosen = channel >= 0 ? channel : alloc_voice();
  if (chosen < 0 || chosen >= g_prg32_audio.max_voices) {
    return -1;
  }

  prg32_audio_sample_slot_t *sample = &g_prg32_audio.samples[sample_id];
  prg32_audio_voice_t *voice = &g_prg32_audio.voices[chosen];
  memset(voice, 0, sizeof(*voice));
  voice->active = true;
  voice->sample_id = sample_id;
  voice->step_fp = pitch_to_step(pitch);
  voice->volume = volume;
  voice->pan = pan;
  voice->loop = (sample->desc.flags & PRG32_AUDIO_SAMPLE_LOOP) != 0u;
  voice->loop_start = sample->desc.loop_start;
  voice->loop_end = sample->desc.loop_end;
  g_prg32_audio.channel_volume[chosen] = volume;
  g_prg32_audio.channel_pan[chosen] = pan;
  return chosen;
}

uint8_t prg32_audio_pan_left_gain(int8_t pan) {
  if (pan < PRG32_AUDIO_PAN_LEFT) {
    pan = PRG32_AUDIO_PAN_LEFT;
  }
  if (pan > PRG32_AUDIO_PAN_RIGHT) {
    pan = PRG32_AUDIO_PAN_RIGHT;
  }
  if (pan <= 0) {
    return 255;
  }
  return (uint8_t)(255 - ((int)pan * 255 / PRG32_AUDIO_PAN_RIGHT));
}

uint8_t prg32_audio_pan_right_gain(int8_t pan) {
  if (pan < PRG32_AUDIO_PAN_LEFT) {
    pan = PRG32_AUDIO_PAN_LEFT;
  }
  if (pan > PRG32_AUDIO_PAN_RIGHT) {
    pan = PRG32_AUDIO_PAN_RIGHT;
  }
  if (pan >= 0) {
    return 255;
  }
  return (uint8_t)(255 - ((int)(-pan) * 255 / (-PRG32_AUDIO_PAN_LEFT)));
}

int prg32_audio_play_sample(uint16_t sample_id, uint8_t volume,
                            uint16_t pitch) {
  return prg32_audio_play_sample_pan(sample_id, volume, pitch,
                                     PRG32_AUDIO_PAN_CENTER);
}

int prg32_audio_play_sample_pan(uint16_t sample_id, uint8_t volume,
                                uint16_t pitch, int8_t pan) {
  prg32_audio_lock();
  int channel = start_voice(-1, sample_id, volume, pitch, pan);
  prg32_audio_unlock();
  return channel;
}

void prg32_audio_stop_channel(int channel) {
  if (channel < 0 || channel >= CONFIG_PRG32_AUDIO_MAX_VOICES) {
    return;
  }
  prg32_audio_lock();
  g_prg32_audio.voices[channel].active = false;
  prg32_audio_unlock();
}

void prg32_audio_stop_all(void) {
  prg32_audio_lock();
  for (uint8_t i = 0; i < CONFIG_PRG32_AUDIO_MAX_VOICES; ++i) {
    g_prg32_audio.voices[i].active = false;
  }
  prg32_audio_unlock();
}

void prg32_audio_note_on(uint8_t channel, uint8_t instrument, uint8_t note,
                         uint8_t volume) {
  prg32_audio_note_on_pan(channel, instrument, note, volume,
                          PRG32_AUDIO_PAN_CENTER);
}

void prg32_audio_note_on_pan(uint8_t channel, uint8_t instrument, uint8_t note,
                             uint8_t volume, int8_t pan) {
  if (instrument >= PRG32_AUDIO_MAX_INSTRUMENTS ||
      !g_prg32_audio.instruments[instrument].present) {
    return;
  }
  prg32_audio_instrument_slot_t *inst = &g_prg32_audio.instruments[instrument];
  uint8_t final_volume = volume ? volume : inst->desc.default_volume;
  int8_t final_pan = pan;
  if (pan == PRG32_AUDIO_PAN_CENTER) {
    final_pan = inst->desc.default_pan;
  }
  uint16_t sample_id = inst->desc.sample_id;
  uint16_t base_note = 60;
  if (sample_id < PRG32_AUDIO_MAX_SAMPLES &&
      g_prg32_audio.samples[sample_id].present) {
    base_note = g_prg32_audio.samples[sample_id].desc.base_note;
  }
  prg32_audio_lock();
  start_voice(channel, sample_id, final_volume, note_pitch(note, base_note),
              final_pan);
  prg32_audio_unlock();
}

void prg32_audio_note_off(uint8_t channel) {
  prg32_audio_stop_channel(channel);
}

static int32_t mix_voice_sample(prg32_audio_voice_t *voice, uint8_t channel) {
  prg32_audio_sample_slot_t *sample = &g_prg32_audio.samples[voice->sample_id];
  uint32_t index = voice->position_fp >> PRG32_AUDIO_FP_SHIFT;
  if (index >= sample->desc.length) {
    if (!voice->loop || voice->loop_end <= voice->loop_start ||
        voice->loop_end > sample->desc.length) {
      voice->active = false;
      return 0;
    }
    index = voice->loop_start;
    voice->position_fp = index << PRG32_AUDIO_FP_SHIFT;
  }

  int32_t pcm = ((int32_t)sample->data[index] - 128) << 8;
  // Apply volumes sequentially. We use bit-shifts (>> 8, effectively dividing by 256)
  // instead of division to maximize performance on the microcontroller.
  // Doing this in steps prevents the 32-bit overflow that happens if we multiply all gains first.
  pcm = (pcm * voice->volume) >> 8;
  pcm = (pcm * g_prg32_audio.channel_volume[channel]) >> 8;
  // Shift by 9 instead of 8 to add 50% headroom (6dB).
  // This prevents harsh clipping/grating when multiple voices play together.
  pcm = (pcm * g_prg32_audio.master_volume) >> 9;
  voice->position_fp += voice->step_fp;

  if (voice->loop && voice->loop_end > voice->loop_start) {
    uint32_t loop_end_fp = voice->loop_end << PRG32_AUDIO_FP_SHIFT;
    uint32_t loop_len_fp = (voice->loop_end - voice->loop_start)
                           << PRG32_AUDIO_FP_SHIFT;
    while (voice->position_fp >= loop_end_fp) {
      voice->position_fp -= loop_len_fp;
    }
  }
  return pcm;
}

void prg32_audio_mix_mono(int16_t *buffer, size_t frames) {
  if (!buffer || frames == 0) {
    return;
  }
  memset(buffer, 0, frames * sizeof(int16_t));
  prg32_audio_lock();
  for (size_t frame = 0; frame < frames; ++frame) {
    int32_t accum = 0;
    for (uint8_t ch = 0; ch < g_prg32_audio.max_voices; ++ch) {
      prg32_audio_voice_t *voice = &g_prg32_audio.voices[ch];
      if (voice->active) {
        accum += mix_voice_sample(voice, ch);
      }
    }
    buffer[frame] = clamp16(accum);
  }
  prg32_audio_unlock();
}

void prg32_audio_mix_stereo(int16_t *buffer, size_t frames) {
  if (!buffer || frames == 0) {
    return;
  }
  memset(buffer, 0, frames * 2u * sizeof(int16_t));
  prg32_audio_lock();
  for (size_t frame = 0; frame < frames; ++frame) {
    int32_t left = 0;
    int32_t right = 0;
    for (uint8_t ch = 0; ch < g_prg32_audio.max_voices; ++ch) {
      prg32_audio_voice_t *voice = &g_prg32_audio.voices[ch];
      if (!voice->active) {
        continue;
      }
      int32_t pcm = mix_voice_sample(voice, ch);
      left += (pcm * prg32_audio_pan_left_gain(voice->pan)) / 255;
      right += (pcm * prg32_audio_pan_right_gain(voice->pan)) / 255;
    }
    buffer[frame * 2u] = clamp16(left);
    buffer[frame * 2u + 1u] = clamp16(right);
  }
  prg32_audio_unlock();
}
