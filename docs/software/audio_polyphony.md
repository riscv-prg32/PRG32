# PRG32 Audio API Reference & Polyphony Guide

The PRG32 platform provides two distinct sets of audio APIs: the **Advanced I2S Mixer API** (for high-quality, asynchronous polyphony and trackers) and the **Legacy Buzzer API** (for simple, blocking sound generation on older hardware).

This document explains every available audio function, how they overlap, and how to manage polyphony effectively.

---

## 1. Advanced Mixer API (Asynchronous)

These functions are part of the modern, non-blocking I2S audio engine. They allow you to play multiple notes simultaneously without pausing your game's execution.

### Voice & Note Control
*   `void prg32_audio_note_on(uint8_t channel, uint8_t instrument, uint8_t note, uint8_t volume);`
    *   **Description:** Starts playing a specific MIDI `note` on a given `channel` using the specified `instrument` ID.
    *   **Usage:** Use this for dynamic in-game sound effects or manual music sequencing.
*   `void prg32_audio_note_on_pan(uint8_t channel, uint8_t instrument, uint8_t note, uint8_t volume, int8_t pan);`
    *   **Description:** Same as above, but explicitly overrides the instrument's default stereo panning (-64 to +63).
*   `void prg32_audio_note_off(uint8_t channel);`
    *   **Description:** Stops the note currently playing on the specified `channel`. For synth instruments, this triggers the ADSR Release phase rather than cutting the audio instantly.
*   `void prg32_audio_note(uint8_t channel, uint8_t instrument, uint8_t note, uint8_t volume, uint32_t duration_ms);`
    *   **Description:** An asynchronous helper that acts like `prg32_audio_note_on`, but automatically schedules the note to turn off after `duration_ms`.
*   `void prg32_audio_stop_channel(int channel);` / `void prg32_audio_stop_all(void);`
    *   **Description:** Instantly kills the audio on a channel (or all channels), bypassing any ADSR release fade-out.

### Melody Playback (Blocking)
*   `void prg32_audio_notes(uint8_t channel, uint8_t instrument, uint8_t volume, const prg32_midi_note_t *notes, size_t count);`
    *   **Description:** Plays a sequential array of MIDI note/duration pairs. **Blocks execution** until the entire sequence finishes.

### Instruments & Asset Loading
To use the advanced API, you must have instruments registered. The engine provides **Instrument 0** (`PRG32_DEFAULT_INSTRUMENT_ID`) by default (a basic pulse wave). You can define additional instruments:
*   **Manual (C Code):** Define a `prg32_instrument_desc_t` (setting waveform type like `PRG32_AUDIO_SYNTH_TRI` and ADSR envelopes) and register it using `prg32_audio_register_instrument(id, &desc)`.
*   **Automated (Data-Driven):** Define instruments in a `.json` file, compile it into a binary blob using `generate_audio.py`, and load everything at once during cartridge init using `prg32_audio_load_assets(blob, size)`.

### PCM Sample Playback
*   `int prg32_audio_play_sample(uint16_t sample_id, uint8_t volume, uint16_t pitch);`
    *   **Description:** Automatically finds a free channel and plays a raw PCM sample. Returns the allocated channel ID (or -1 if no channels are free).
*   `int prg32_audio_play_sample_pan(uint16_t sample_id, uint8_t volume, uint16_t pitch, int8_t pan);`
    *   **Description:** Same as above, but allows you to specify stereo panning.

### Tracker Engine
*   `void prg32_audio_play_track(uint16_t track_id);`
    *   **Description:** Arms the background tracker engine to begin playing the sequence of events defined in the specified track ID.
    *   **Note:** The tracker shares the same channels/voices as the manual APIs!
*   `void prg32_audio_stop_track(void);`
    *   **Description:** Stops the tracker from advancing. Does *not* automatically send `note_off` to currently playing channels.
*   `void prg32_audio_set_tempo(uint16_t bpm);`
    *   **Description:** Dynamically changes the beats-per-minute of the tracker engine.

### Global & Channel Mixing
*   `void prg32_audio_set_master_volume(uint8_t volume);`
*   `void prg32_audio_set_channel_volume(uint8_t channel, uint8_t volume);`
*   `void prg32_audio_set_channel_pan(uint8_t channel, int8_t pan);`

---

## 2. Legacy / Buzzer API (Synchronous)

> [!WARNING]
> **Blocking Behavior:** These legacy APIs are **strictly blocking**. When you call them, your game's CPU thread will sleep (`vTaskDelay`) until the sound finishes playing! They are prefixed with `prg32_buzzer_` and are designed to drive the passive PWM hardware directly.

*   `void prg32_buzzer_tone(uint32_t hz, uint32_t ms, uint16_t duty);`
    *   **Description:** Plays a tone directly on the hardware buzzer with a specific frequency (Hz) and PWM duty cycle. Blocks execution.
    *   **Note:** If the hardware buzzer is disabled (`PRG32_PIN_BUZZER < 0`), this gracefully routes a fallback square wave to the advanced I2S mixer (temporarily hijacking Channel 0 and Instrument 31) to emulate the buzzer.
*   `void prg32_buzzer_play_notes(const prg32_note_t *notes, size_t count);`
    *   **Description:** Plays a sequential array of raw frequency/duration pairs on the buzzer. Blocks execution.
*   `void prg32_buzzer_sample_u8(const uint8_t *samples, size_t count, uint32_t sample_rate);`
    *   **Description:** A highly legacy function that uses CPU microsecond delays (`esp_rom_delay_us`) to manually bit-bang 8-bit PCM audio directly to the PWM buzzer for low-quality digital audio.

---

## 3. Polyphony & Channel Management

Polyphony (playing multiple sounds at once) is exclusively handled by the **Advanced Mixer API** by utilizing different **channels** (0 through `MAX_VOICES - 1`).

### Manual Polyphony
To play a chord manually, issue multiple non-blocking `prg32_audio_note_on` (or `prg32_audio_note`) calls targeting different channels:
```c
prg32_audio_note(0, PRG32_DEFAULT_INSTRUMENT_ID, 60, 255, 500); // C on channel 0
prg32_audio_note(1, PRG32_DEFAULT_INSTRUMENT_ID, 64, 255, 500); // E on channel 1
prg32_audio_note(2, PRG32_DEFAULT_INSTRUMENT_ID, 67, 255, 500); // G on channel 2
```

### Automated Polyphony (Tracker)
When authoring a background music track in the data-driven tracker format, polyphony is achieved using the `delta` (time delay) parameter. Setting `delta = 0` tells the tracker to execute the next event in the exact same frame.
```json
[
  {"delta": 0, "command": "NOTE_ON", "arg0": 0, "arg1": 60},
  {"delta": 0, "command": "NOTE_ON", "arg0": 1, "arg1": 64},
  {"delta": 4, "command": "NOTE_ON", "arg0": 2, "arg1": 67}
]
```
*(The engine processes the first three notes instantly before waiting 4 ticks).*

### Resolving API Conflicts
Because the automated Tracker Engine and your manual C code share the same physical channel pool, they can cut each other off. If the tracker plays a note on Channel 4, it is literally calling `prg32_audio_note_on(4, ...)` under the hood.

**Best Practice:**
Partition your voices explicitly.
1. When composing your `.json` tracker music, restrict it to using channels `0` through `5`.
2. When triggering sound effects from your game code, ensure you only call `prg32_audio_note_on` or `prg32_audio_play_sample` targeting channels `6` and `7`.
3. Avoid the Legacy Buzzer APIs entirely in modern cartridges unless you are building a simple, single-threaded application that doesn't need to run game logic while sound is playing.
