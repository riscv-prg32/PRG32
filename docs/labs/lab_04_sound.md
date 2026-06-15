# Lab 04 - Sound and Timing

## Goal

Add short tones, note sequences, or samples for events without making the game
feel stuck.

## Steps

1. Call `prg32_audio_beep` during initialization.
2. Add a second beep when a collision occurs.
3. Use a short duration first:

```asm
li a0, 880
li a1, 20
call prg32_audio_beep
```

4. Try `200 ms` and observe how it changes responsiveness.
5. Move the beep so it triggers only once per collision.
6. In C, try `prg32_audio_note(69, 80)` for an A4 note.
7. Use `tools/prg32_audio_convert.py` to prepare a short WAV effect.

## Checkpoint

The game should make a sound on collision and continue moving smoothly. Advanced
students can compare `prg32_audio_beep`, `prg32_audio_play_notes`, and
`prg32_audio_sample_u8`.

QEMU can verify that the code path reaches the audio calls, but final sound
behavior must be checked on hardware with the passive buzzer or MAX98357A audio
board.

## Extension: I2S Audio

1. Wire one MAX98357A as shown in `docs/audio.md`.
2. Run `examples/features/audio_mono_beep`.
3. Replace the generated square sample with a short WAV converted through
   `tools/wav2prg32sample.py`.
4. Pack the sample with `tools/prg32audio_pack.py`.
5. Build a cartridge with `python3 -m prg32 build --audio-block`.

Checkpoint: the cartridge can call `prg32_audio_play_sample(0, 255, 1024)` and
hear sample `0` without reflashing the resident firmware.

## Reflection

Why does a blocking audio helper make timing easier to understand but worse for fast games?
