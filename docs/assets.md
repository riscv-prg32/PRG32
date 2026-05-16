# PRG32 Asset Tools

PRG32 includes small host tools for turning classroom media into C or assembly
source files.

## Images, Sprites, Tiles, and GIF Frames

Convert an image to a C RGB565 array:

```bash
python3 tools/prg32_image_convert.py player.png \
  --symbol player_sprite \
  --width 16 \
  --height 16 \
  --format c \
  --out build/player_sprite.c
```

Convert an animated GIF to assembly frames:

```bash
python3 tools/prg32_image_convert.py walk.gif \
  --symbol walk_frames \
  --width 16 \
  --height 16 \
  --frames 4 \
  --format asm \
  --out build/walk_frames.S
```

Use `--crop x,y,w,h` before resizing when only part of the image should be
converted. The converter uses nearest-neighbor resizing so pixel-art edges stay
teachable.

For guided preparation, run:

```bash
python3 tools/prg32_image_prepare.py player.png
```

The interactive helper asks for asset type, crop, target size, symbol, output
format, and output path.

## Audio Samples and MIDI-Like Tracks

Convert a WAV file to an unsigned 8-bit sample array:

```bash
python3 tools/prg32_audio_convert.py jump.wav \
  --symbol jump_sample \
  --sample-rate 8000 \
  --format c \
  --out build/jump_sample.c
```

Convert a MIDI file to a `prg32_note_t` melody:

```bash
python3 tools/prg32_audio_convert.py theme.mid \
  --symbol theme_notes \
  --format c \
  --out build/theme_notes.c
```

WAV conversion uses the Python standard library. Image conversion requires
Pillow. MIDI conversion requires `mido`.

```bash
python3 -m pip install pillow mido
```

Generated arrays can be included in firmware examples or packaged into
uploadable cartridges when they fit inside the cartridge RAM limit.
