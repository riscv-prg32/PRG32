# PRG32 Audio Engine: Notes, Instruments, and Channels

This document explains the core mechanics of sound generation in the PRG32 system, focusing on `prg32_audio_note_on`, `prg32_audio_note_off`, and how instruments and channels interact.

## 1. Channels (Voices)

A **channel** (or **voice**) represents a physical slot that can synthesize or play a single sound at a given time. The PRG32 engine supports up to `CONFIG_PRG32_AUDIO_MAX_VOICES` simultaneous voices (typically 8 to 16).

Each channel tracks its own independent state in memory:
- Whether it is currently `active`.
- Its volume and stereo panning.
- The progression of its ADSR (Attack, Decay, Sustain, Release) envelope.
- Its current position in a PCM sample OR the phase of its procedural oscillator.

When you interact with the mixer (e.g., using `prg32_audio_note_on`), you usually specify which channel to use. If you reuse a channel that is already playing a note, the previous note on that channel is immediately cut off and replaced by the new note.

## 2. Instruments

An **instrument** defines *how* a note should sound. They are defined via the `prg32_instrument_desc_t` structure, which contains:
- **`sample_id`**: Determines the sound source.
  - If the highest bit (`0x8000`) is set, it indicates a **procedural synth** instrument (Triangle, Sawtooth, Pulse, or Noise wave), and the remaining bits define parameters like filter cutoff and pulse width.
  - Otherwise, it refers to an index in the loaded **PCM sample** array.
- **ADSR Envelope**: `attack`, `decay`, `sustain`, and `release` values define how the volume of the note shapes over time.
- **Default Panning and Volume**: The default mix parameters if none are explicitly provided.

> [!NOTE]
> In the context of the PRG32 tracker engine (like the one used in `bachdemo`), **the instrument ID is implicitly mapped 1:1 to the channel number**. This means events on channel `0` will always use instrument `0`, channel `1` uses instrument `1`, and so on.

## 3. Triggering a Note (`prg32_audio_note_on`)

When `prg32_audio_note_on(uint8_t channel, uint8_t instrument, uint8_t note, uint8_t volume)` is called, the mixer executes the following sequence:

1. **Instrument Lookup**: It attempts to load the requested `instrument` from the internal `g_prg32_audio.instruments` array. If the instrument ID isn't registered, the function immediately aborts, and **no sound is played**.
2. **Pitch Conversion**: It takes the provided MIDI `note` (e.g., `60` for Middle C) and converts it into an internal fractional pitch-stepping value (`voice->step_fp`), determining how fast the wave or sample should be read to produce the correct frequency.
3. **Voice Allocation**: It targets the requested `channel`. It resets the state for this channel.
4. **Initialization**:
   - For **synth** instruments: The ADSR envelope is reset to the "Attack" phase, and the oscillator phase is reset.
   - For **PCM** instruments: The playback position is reset to the beginning of the sample (or to its configured `loop_start`).
5. **Activation**: The voice is marked as `active = true`, causing the background audio task to immediately start rendering it into the audio buffer during the next mix cycle.

## 4. Releasing a Note (`prg32_audio_note_off`)

When `prg32_audio_note_off(uint8_t channel)` is called, it signals the mixer to stop the note on that channel. However, the behavior differs depending on the instrument type:

- **For Procedural Synth Instruments**:
  The note **does not stop immediately**. Instead, the engine triggers the "Release" phase of the ADSR envelope. The sound will gradually fade out based on the duration defined by the instrument's `release` parameter. The channel only becomes `active = false` once the envelope fully reaches a volume of zero.
- **For PCM Sample Instruments**:
  The channel is immediately marked as `active = false`. Playback stops instantly without any trailing envelope fade-out.

> [!TIP]
> This difference is why `bachdemo`'s tracker issues explicit `NOTE_OFF` commands before the end of the song. Without a `NOTE_OFF`, the synth notes would sustain infinitely at their configured `sustain` volume level.
