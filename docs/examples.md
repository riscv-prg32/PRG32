# PRG32 Examples

PRG32 examples are split into game examples and focused feature examples.

## Audio Examples

| Example | Purpose | Hardware |
|---|---|---|
| `examples/audio_mono_beep` | generated mono tone smoke test | one MAX98357A |
| `examples/audio_mono_sample` | unsigned 8-bit PCM sample playback | one MAX98357A |
| `examples/audio_mono_tracker` | tracker event sequence | one MAX98357A |
| `examples/audio_stereo_pan_test` | verify left/right channel selection | two MAX98357A boards |
| `examples/audio_stereo_music` | centered music plus panned effects | two MAX98357A boards |

Run audio examples as temporary firmware apps while validating hardware. For
uploadable cartridges, pack assets with `tools/prg32audio_pack.py` and pass
`--audio-block` to `tools/prg32_game.py build`.

## Game Examples

Game examples live under `examples/games`. Each game is organized with ASCII
assembly, graphics assembly, and C sources when available:

```text
examples/games/<name>/
|-- README.md
|-- ascii/game.S
|-- graphics/game.S
`-- c/game.c
```

Use these for Computing Architecture and C Programming classes.

The platformer and raycaster C examples are the fuller playable companions to
the setup-launched demo pages. The assembly versions remain compact so students
can trace registers, stack frames, and ABI calls without losing the main idea.

## Feature Examples

Feature demos live under `examples/features` and isolate one framework topic:

- scrolling and parallax
- animated sprites
- dual playfield rendering
- 320x200 game splash screens
- status-band overlays
- tile/platform helpers
- joystick keyboard input
- Wi-Fi setup
- player 2 input
- audio synthesis

The setup-launched device demo is not a cartridge example, but it is useful for
trainers: it checks framework capabilities and includes small 320x200 sketches
inspired by Pong, Breakout, Space Invaders, Pacman, Tetris, Pole Position, and
Asteroids, plus a tile-engine platformer and a Doom-style fixed-point
raycaster.
