# `prg32_audio_play_track` Execution Flow

This document explains exactly how `prg32_audio_play_track` works step-by-step, from the initial function call to the underlying audio generation.

## 1. Initial Invocation

When your cartridge calls `prg32_audio_play_track(uint16_t track_id)` (e.g., `prg32_audio_play_track(0)` in `bach_stereo.c`), the function itself does very little. It simply primes the audio tracker's state:

1. It checks if the `track_id` is within bounds (`< PRG32_AUDIO_MAX_TRACKS`) and if the track is actually loaded into memory (`g_prg32_audio.tracks[track_id].present`).
2. It locks the audio mutex to prevent race conditions.
3. It sets `g_prg32_audio.tracker.active = true`.
4. It resets the tracker state variables:
   - `event_index = 0` (start at the first event)
   - `tick_accum = 0` (reset accumulated time)
   - `next_delta = 0` (process the first event immediately)
5. It unlocks the mutex and returns.

At this point, **no audio has been generated yet**. The tracker has simply been armed.

## 2. The Background Audio Task

The PRG32 system runs a dedicated FreeRTOS task named `audio_task` (defined in `audio.c`). This task loops continuously as long as the system is running.

In every iteration of this loop:
1. It calculates how much real time has passed since the last iteration (`elapsed_ms`).
2. It calls `prg32_audio_tracker_step(elapsed_ms)` to advance the tracker.
3. It mixes the active audio voices into a buffer (via `prg32_audio_mix_stereo` or `prg32_audio_mix_mono`).
4. It writes the mixed buffer to the hardware I2S peripheral (or to QEMU's virtual audio output).

## 3. Advancing the Tracker (`prg32_audio_tracker_step`)

This is where the actual track data is interpreted. Inside `audio_tracker.c`, `prg32_audio_tracker_step` receives the elapsed time and does the following:

1. It adds `elapsed_ms` to `tracker->tick_accum`.
2. It enters a `while` loop that runs as long as the track is active.
3. Inside the loop, it calculates `ms_per_tick` based on the tracker's current tempo (BPM). If `tick_accum < ms_per_tick`, it breaks out of the loop. Calculating this inside the loop ensures dynamic tempo changes take effect instantly.

Inside this `while` loop, the tracker handles timing and event execution:
- **If it's waiting:** If `tracker->next_delta > 0`, it subtracts `ms_per_tick` from `tick_accum`, decrements `next_delta`, and loops. (This is how it waits between notes).
- **If it's time to play:** If `tracker->next_delta == 0`, it fetches the next `prg32_audio_event_t` from the track data.
- It sets `tracker->next_delta = event.delta_ticks`. This establishes the wait time *after* the current event.
- It calls `execute_event(&event)` to actually perform the action.

> [!NOTE]
> Because it evaluates `next_delta == 0` first without consuming a tick, multiple events with `delta_ticks = 0` (like a chord) are processed instantly in the exact same real-time tick.

## 4. Executing Events (`execute_event`)

The `execute_event` function acts as a large `switch` statement for the different tracker commands:

- **`PRG32_AUDIO_CMD_NOTE_ON`**:
  It calls `prg32_audio_note_on(...)`. It uses the event's `arg0` as the channel number. It passes the **channel number as both the channel AND the instrument ID**. It uses `arg1` as the MIDI note.
- **`PRG32_AUDIO_CMD_NOTE_OFF`**:
  It calls `prg32_audio_note_off(event->arg0)`.
- **`PRG32_AUDIO_CMD_SET_VOLUME`**:
  It updates the internal `g_prg32_audio.channel_volume` array for the given channel.
- **`PRG32_AUDIO_CMD_SET_PAN`**:
  It updates the panning for the given channel.
- **`PRG32_AUDIO_CMD_SET_TEMPO`**:
  It updates the tracker's internal BPM, which immediately changes the `ms_per_tick` calculation.

## 5. Playing the Note (`prg32_audio_note_on`)

When a `NOTE_ON` event occurs, the execution reaches `prg32_audio_note_on` in `audio_mixer.c`.

1. It looks up the instrument definition (`g_prg32_audio.instruments[instrument]`). Remember, for tracks, the instrument ID is implicitly the channel number.
2. If the instrument isn't registered or isn't present, **it silently ignores the note**.
3. It converts the requested MIDI note into an internal pitch step value using a ratio lookup table (`note_pitch()`).
4. It allocates the requested channel (or finds a free voice) in the `g_prg32_audio.voices` array.
5. It configures the voice:
   - Sets `voice->active = true`.
   - Copies the instrument's envelope parameters (attack, decay, sustain, release).
   - If it's a procedural synth (indicated by the `PRG32_AUDIO_SYNTH_MARKER` bit), it initializes the waveform generator.
   - If it's a PCM sample, it sets up the sample start/loop points.

## 6. Audio Generation

Once the voice is marked as `active`, the next iteration of the `audio_task`'s mixing phase (`prg32_audio_mix_stereo` or `prg32_audio_mix_mono`) will see it.

The mixer will step through the voice's synth generator (or PCM sample buffer), apply the ADSR envelope, apply the channel volume and master volume, apply the stereo panning, and accumulate the result into the final audio buffer sent to the speakers.
