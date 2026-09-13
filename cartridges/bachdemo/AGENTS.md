# Contributor guidance

This is an external PRG32 cartridge targeting `riscv-prg32/PRG32` branch
`development-c6`.

- Preserve the portable cartridge ABI and the `bach_stereo` entry prefix.
- Regenerate `assets/audio.json` through `scripts/generate_audio.py`; do not
  hand-edit the generated score.
- Keep the score descriptor-only: do not add recordings or third-party samples.
- Preserve all four synth waveform families and channels 0 through 7.
- Run `scripts/test.sh` after every source or score change.
- Update `docs/audio-design.md` when changing synthesis or spatialization.
- Document the provenance and license of any added musical material.
