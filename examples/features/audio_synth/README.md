# SID-Like Stereo Synthesis Demo

This cartridge demonstrates four procedural instruments without waveform PCM
assets: centered triangle bass, left pulse lead, right saw harmony, and noise
percussion that alternates sides.

Controls:

- A: start the three-voice chord.
- B: release the chord through each instrument's ADSR envelope.
- Left or Right: trigger the noise instrument on the opposite side.
- Select: play the same chord and releases through tracker events.

Pack the descriptor-only AUDIO block, then build the cartridge:

```bash
python3 tools/prg32audio_pack.py \
  examples/features/audio_synth/audio.json \
  --out build-esp32c6/audio-synth.block
python3 -m prg32 cartridge build \
  examples/features/audio_synth/c/demo.c \
  --portable \
  --entry-prefix audio_synth_c \
  --name audio-synth \
  --audio-block build-esp32c6/audio-synth.block \
  --out build-esp32c6/audio-synth.prg32
```

The numeric `sample_id` values in `audio.json` are synth-ID encodings documented
in [`docs/tools/audio.md`](../../../docs/tools/audio.md). C firmware code should
normally use the named `PRG32_AUDIO_SYNTH_*` macros instead.
