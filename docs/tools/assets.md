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

For a 24x24 multicolor sprite, use `--width 24 --height 24`. The generated
array must contain `24 * 24` RGB565 halfwords and can be drawn with
`prg32_sprite_draw_24x24(x, y, sprite)`. White pixels (`0xffff`,
`PRG32_COLOR_WHITE`) are transparent in the fixed-size 16x16 and 24x24 sprite
helpers, which lets simple classroom assets keep a visible background without a
separate alpha mask.

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
uploadable cartridges when they fit inside the 128 KiB cartridge package limit.

## Compact Indexed and Bitplane Sprites

RGB565 remains the default and is the right format when every pixel needs an
independent 16-bit color. Animations with a small shared palette can use packed
indices or planar bitplanes instead:

```bash
python3 tools/prg32_image_convert.py walk.gif \
  --symbol walk \
  --width 16 --height 16 --frames 4 \
  --mode indexed --colors 16 --transparent-index 0 \
  --format c --out build/walk.c
```

Use `--mode bitplanes` with the same arguments for plane-major data. Supported
palette limits are 2, 4, 8, 16, and 256 colors. Packed indexed mode supports
1, 2, 4, and 8 bits per pixel; bitplanes additionally support 3 planes for an
8-color asset. Four-bit data stores two pixels per byte; eight-bit data stores
one pixel per byte. A 16-color asset uses 4 bits per pixel instead of RGB565's
16, so its pixel data is one quarter of the size; its RGB565 palette is stored
once.

Use 4-bpp indexed data as the normal default for retro-style game sprites. It
balances a small 16-color palette, two pixels per source byte, and a specialized
two-pixel renderer. Choose the other formats deliberately:

| Format | Recommended use |
|---|---|
| 1-bpp indexed | Fonts, masks, and monochrome graphics |
| 2-bpp indexed | Four-color artwork |
| **4-bpp indexed** | **General-purpose PRG32 game sprites** |
| 8-bpp indexed | Detailed sprites requiring up to 256 colors |
| RGB565 | Unrestricted color or minimum raw decoding work |

The palette is built deterministically in first-seen RGB565 color order across
all frames and is shared by the whole animation. Conversion is exact at the
native RGB565 level: if the image contains more colors than `--colors` allows,
the command fails instead of silently quantizing the artwork.

The generated C or assembly defines a `prg32_indexed_sprite_t` descriptor.
Draw packed data with `prg32_sprite_draw_indexed(x, y, &walk, frame)` and planar
data with `prg32_sprite_draw_bitplanes(x, y, &walk, frame)`. Decoding writes
straight to the existing RGB565 renderer and does not allocate a temporary
uncompressed frame. Frames are independently byte-padded, which makes frame
addressing deterministic for animation.

Packed indices are continuous within each frame and put the first pixel in the
most-significant bits (`0x3a` means indices 3 then 10 in 4-bit mode). Bitplane
frames store plane 0 through plane N-1; every plane stores top-to-bottom rows,
and every row starts on a byte boundary. Within a row, pixels 0 through 7 map to
bits 7 through 0. A partial final byte is padded with zero bits.

Generated files also define `WALK_SPRITE`, a tagged pointer accepted by the
existing `prg32_sprite_draw_16x16`, `prg32_sprite_draw_24x24`,
`prg32_sprite_draw_frame`, and `prg32_sprite_anim_init` calls. Consequently the
existing `prg32_sprite_anim_update` / `prg32_sprite_anim_draw` workflow selects
and palette-expands compact frames automatically. C output uses the public
pointer-tag macro; assembly output emits the equivalent tagged symbol alias.
Ordinary aligned RGB565 pointers never carry the compact tag, so their drawing
and transparent-color argument retain their original semantics.

### Storage comparison

For width `W`, height `H`, and `F` frames, RGB565 pixels require `2*W*H*F`
bytes. Indexed8 needs `W*H*F + 2*palette_count`; indexed4 needs
`ceil(W*H/2)*F + 2*palette_count`. N row-aligned bitplanes need
`ceil(W/8)*H*F*N + 2*palette_count`. Each compact asset also has one small
descriptor.

For a 16x16, four-frame animation with 16 colors, RGB565 pixel data is 2048
bytes. Indexed4 and four bitplanes each use 512 encoded bytes plus a 32-byte
palette: 544 bytes before the descriptor, a 73.4% reduction. Indexed4 has the
simpler random lookup. Bitplanes are most useful when plane-oriented masking or
selective effects justify their row padding and extra plane reads.

For one 32x32 frame, pixel data alone costs 2048 bytes in RGB565, 1024 bytes at
8 bpp, 512 bytes at 4 bpp, 256 bytes at 2 bpp, or 128 bytes at 1 bpp. A maximum
palette adds 512, 32, 8, or 4 bytes respectively to the indexed formats. A
shared animation palette amortizes that fixed cost across every frame.

`--transparent-index -1` keeps every palette entry opaque. Any other value
skips that generated palette index while drawing. Inspect the emitted palette
when choosing an index manually. The embedded Performance Test uses a matched
2-bpp RGB565/indexed probe to measure the decoding tradeoff; see
[Performance Metrics](/docs/measurement/metrics_api.md).

## Cartridge AUDIO Blocks

For PRG32 I2S audio, prefer raw unsigned 8-bit sample data plus an AUDIO block:

```bash
python3 tools/wav2prg32sample.py kick.wav \
  --rate 22050 \
  --normalize \
  --trim-silence \
  --out build/kick.raw
```

Create `audio.json`:

```json
{
  "samples": [{"file": "kick.raw", "base_note": 60}],
  "instruments": [{"sample_id": 0, "default_volume": 255, "default_pan": 0}],
  "tracks": [
    {
      "events": [
        {"delta": 0, "command": "PLAY_SAMPLE", "arg0": 0, "arg1": 255},
        {"delta": 6, "command": "END", "arg0": 0, "arg1": 0}
      ]
    }
  ]
}
```

Pack it:

```bash
python3 tools/prg32audio_pack.py audio.json --out build/audio.block
```
