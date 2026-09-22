# PRG32 Cartridges

This directory contains in-tree PRG32 cartridges. The main demos include
store-ready metadata and assets; Audio Pan Test is a source-only diagnostic.
Each can be built as a portable package for supported PRG32 targets.

- [Audio Pan Test](audiotest/README.md) cycles notes through stereo positions
  and reports the selected audio mode.

- [Bach Stereo Showcase](bachdemo/README.md) demonstrates eight-voice
  procedural audio with multiple waveforms, envelopes, filtering, and stereo
  panning through an original compact arrangement of Bach's *Prelude in C
  major*.
- [Blackjack](blackjack/README.md) is a complete casino blackjack game with
  solo and multiplayer modes, tested game rules, indexed graphics, persistent
  scores, and procedural audio.
- [PRG32 C](c-language/README.md) is a C programming environment: a
  syntax-coloured editor, a one-pass compiler and a bounds-checked virtual
  machine driven by a Bluetooth keyboard (or the QEMU console).
- [DeviceDemo+](devicedemo/README.md) is a cartridge-level hardware and runtime
  feature showcase covering graphics, input, audio, diagnostics, Wi-Fi, and
  score APIs.
- [Performance Test](performancetest/README.md) is the reference cartridge for
  reproducible measurements through the public performance broker ABI.
- [Poing](poing/README.md) is a real-time procedural graphics showcase featuring
  a textured bouncing sphere, perspective grid, lighting, and synchronized
  impact audio.
