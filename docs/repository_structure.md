# Repository Structure

This document outlines the high-level layout of the PRG32 repository.

## Repo Map

- `components/prg32`: runtime implementation
- `main`: default firmware app
- `examples/games`: assembly and C game demos
- `examples/features`: focused firmware feature demos
- `python3 -m prg32`: cartridge tooling
- `tools/prg32_image_convert.py`: image and animation converter
- `tools/prg32_audio_convert.py`: sample and MIDI converter
- `tools/prg32audio_pack.py`: AUDIO block packer
- Classroom score server:
  [riscv-prg32/ScoreServer](https://github.com/riscv-prg32/ScoreServer)
- Multiplayer relay:
  [riscv-prg32/MultiplayerServer](https://github.com/riscv-prg32/MultiplayerServer)
- `docs`: tutorials, labs, API docs
- `.github/workflows/ci.yml`: GitHub Actions smoke and firmware build workflow
- `tests`: host-side unit tests for tooling and documentation hygiene

## Repository Structure (Academic View)

- `components/prg32`: framework API/ABI and hardware abstraction layer
- `main`: reference runtime firmware app (minimal and teachable baseline)
- `examples/games`: course demos in RISC-V assembly and C
- `examples/features`: focused rendering demos in RISC-V assembly and C
- `docs/labs`: lab handouts, debugging assignments, and assessment-friendly exercises
- `tools`: reproducible developer tooling (cartridge builder, QEMU scripts,
  setup performance report tooling)
- `hardware`: architecture notes and board integration scaffold
