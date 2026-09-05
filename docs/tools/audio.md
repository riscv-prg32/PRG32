# PRG32 Audio

PRG32 audio is a small retro-style digital audio runtime for classroom games.
It keeps the first steps as simple as a buzzer command, while giving students a
path toward PCM samples, SID-like procedural synthesis, tracker music, and
stereo panning.

The setup menu includes an audio page that auto-detects the currently usable
output path: none, PWM buzzer, mono I2S, or stereo I2S. Trainers can adjust the
test volume, play a short tune, and enable the onboard RGB LED as a
spectrum-style VU meter when the LED GPIO is available.

## Overview

The audio stack has four pieces:

```text
PRG32 program
|-- sample, note, and track API calls
|-- AUDIO cartridge block descriptors and optional PCM assets
|-- signed 16-bit mono/stereo mixer
`-- ESP-IDF I2S output to MAX98357A amplifier boards
```

The runtime mixes unsigned 8-bit mono samples and procedural synth voices as
signed 16-bit PCM. Integer and fixed-point arithmetic keep the real-time path
teachable and avoid floating point inside the mixer.

## Audio Modes

Mono audio is the default and mandatory mode. Stereo audio is optional and is
documented as PRG32 Audio Plus.

| Mode | Hardware | Output | Voices |
|---|---|---|---:|
| Mono | one MAX98357A and one 4-8 ohm speaker | duplicated mono I2S frames | 6 |
| Stereo | two MAX98357A boards and two speakers | interleaved left/right I2S | 6-8 |

Mono programs run unchanged on stereo hardware. Stereo programs should call
`prg32_audio_get_mode()` before depending on pan behavior. In mono mode, pan
arguments are accepted and ignored by the output stage.

## Mono Mode

Mono mode is the classroom baseline:

| Feature | Value |
|---|---|
| Sample rate | 22050 Hz |
| Source samples | unsigned 8-bit PCM mono |
| Internal mixer | signed 16-bit PCM |
| Default voices | 6 |
| Amplifier | one MAX98357A |

Initialize the default mono configuration:

```c
#include "prg32_audio.h"

void app_main(void) {
    prg32_audio_init(NULL);
}
```

## Stereo Mode

Stereo mode uses one shared I2S stream and two MAX98357A boards. Each board must
be configured by its jumper or mode pin to reproduce only one channel.

```c
prg32_audio_config_t audio = {
    .sample_rate = 22050,
    .mode = PRG32_AUDIO_MODE_STEREO,
    .max_voices = 8,
    .gpio_bclk = 4,
    .gpio_lrclk = 11,
    .gpio_data = 23,
    .gpio_sd = -1,
};
prg32_audio_init(&audio);
```

Pan range:

```text
-64 = full left
  0 = center
 63 = full right
```

## Supported Hardware

The reference audio amplifier is a MAX98357A I2S DAC/amplifier breakout.

Required for mono:

- one MAX98357A board
- one 4-8 ohm speaker, 1-3 W

Optional for stereo:

- two MAX98357A boards
- two 4-8 ohm speakers, 1-3 W

Do not connect the MAX98357A speaker outputs directly to headphones or line-in
inputs. They are bridged speaker outputs.

## MAX98357A Wiring

See [`hardware.md`](/docs/hardware/hardware.md) for the PRG32 audio pin configurations.

The required I2S pins avoid the reference LCD, joystick, and passive buzzer
wiring. If a MAX98357A breakout needs explicit shutdown control, assign
`CONFIG_PRG32_AUDIO_I2S_SD_GPIO` to another unused GPIO in menuconfig; otherwise
leave SD at `-1` and wire the board for its default enabled state.

On the Adafruit MAX98357A breakout, `SD` is also the channel-mode pin. The
breakout's default resistor network enables stereo-average mono output, and
PRG32 duplicates mono samples into both I2S slots, so a single board will play
even if `SD` selects only left or only right. Do not tie `SD` directly to GND;
that shuts the amplifier down. Powering the board from 3.3 V is valid, though
5 V gives more speaker power.

The firmware cannot electrically detect whether a MAX98357A is plugged in. The
setup menu reports mono I2S when the firmware is built for physical ESP32-C6,
audio is enabled, and the I2S driver starts on the configured pins. QEMU display
builds disable physical I2S output, but seamlessly redirect the audio PCM stream
over the UART port to the host machine for playback.

Stereo wiring shares the same I2S signals across two boards (Left and Right). See [`hardware.md`](/docs/hardware/hardware.md) for the full diagram.

Configure the left board for the left I2S channel and the right board for the
right channel. Breakout labels vary: common names include `L/R`, `GAIN`,
`SD_MODE`, `SD`, and `MODE`. Check the exact board pinout before soldering.

## Bill Of Materials

See the [central bill of materials and purchasing guide](../hardware/where_to_buy.md)
for mono and stereo quantities, optional connectors and country-specific suppliers.

## Assembly Instructions

Mono:

1. Disconnect power from the ESP32-C6 board.
2. Connect ESP32-C6 GND to MAX98357A GND.
3. Connect ESP32-C6 3V3 or 5V to MAX98357A VIN.
4. Connect GPIO4 to BCLK, GPIO11 to LRC/WS, and GPIO23 to DIN.
5. Leave SD in the breakout's default enabled state, or connect it to a
   menuconfig-selected unused GPIO.
6. Connect the speaker to the MAX98357A speaker `+` and `-` outputs.
7. Power the board and run `examples/features/audio_mono_beep`.

Stereo:

1. Disconnect power.
2. Label the two MAX98357A boards `LEFT` and `RIGHT`.
3. Share power, ground, BCLK, LRCLK, DATA, and optional SD across both boards.
4. Configure the left board for left-channel output.
5. Configure the right board for right-channel output.
6. Connect one speaker to each board.
7. Run `examples/features/audio_stereo_pan_test`.

## Audio API

Core calls:

- `prg32_audio_init(config)`: initialize the I2S mixer runtime.
- `prg32_audio_shutdown()`: stop the audio task and I2S driver.
- `prg32_audio_get_mode()`: return mono or stereo.
- `prg32_audio_register_sample(...)`: register a C sample array.
- `prg32_audio_play_sample(sample_id, volume, pitch)`: play centered sample.
- `prg32_audio_play_sample_pan(sample_id, volume, pitch, pan)`: play with pan.
- `prg32_audio_note_on(channel, instrument, note, volume)`: start an instrument.
- `prg32_audio_note_off(channel)`: stop a voice.
- `prg32_audio_play_track(track_id)`: start a tracker event stream.
- `prg32_audio_set_master_volume(volume)`: set global volume.
- `prg32_audio_led_vu_enable(enabled)`: let audio tests and PWM helpers drive
  the RGB LED VU meter.

Pitch uses `1024` as the natural sample speed. Volumes use `0..255`.

**I2S vs PWM Audio API Differences:**
- **I2S Engine (`prg32_audio_note`)**: Uses standard MIDI **notes** (e.g. 60 for Middle C) and true audio synthesis. It automatically reads your configured wave samples and pitches them perfectly to the musical note.
- **PWM Buzzer (`prg32_audio_tone`)**: Buzzers cannot play complex samples, they only pulse a pin ON/OFF. Thus, they require raw **frequencies** (e.g. 262 Hz for Middle C) and a **duty cycle** parameter. The duty cycle acts as the buzzer's volume control by reducing the ON/OFF percentage, thereby limiting electrical power.

## Volume Scaling

The internal audio engine uses a 0-255 scale for sample volumes, master volumes, and I2S amplitude. However, human hearing perceives sound intensity logarithmically, and cheap hardware amplifier breakouts often brown out or heavily clip signals near their absolute mathematical maximum limits.

To present a safe, human-friendly 0-100% volume slider to the user, PRG32 maps the 0-100% UI value to a custom, capped absolute scale using a **mixed linear/quadratic curve**:

- **Linear Component (30%)**: Ensures that low percentages (like 5%) are immediately audible and mathematically round up to non-zero values.
- **Quadratic Component (70%)**: Smoothly ramps up the volume to match the logarithmic sensitivity of the human ear, keeping the standard "sweet spot" comfortable around 50%.
- **Clipping (Max 70)**: Caps the absolute maximum internal output volume at 70/255 to prevent severe electrical clipping and power supply strain on basic 3-watt speakers.

## Global Master Volume (NVS)

PRG32 automatically handles global volume state for all cartridges. The system loads the user's volume preference from the `volume_pct` key in the `prg32` NVS namespace on boot (defaulting to the config parameter PRG32_AUDIO_DEFAULT_VOLUME_PCT which is set to 70%.).
Cartridges do **not** need to manually manage master volume or read from NVS.

## Cartridge Audio Usage

Cartridges should be completely agnostic to the system's global volume or whether audio is disabled entirely. If the user disabled audio in `menuconfig`, the API functions simply act as harmless stubs.

To play a simple tone or note from a cartridge you can use the ABI macro:
```c
// Play Middle C (MIDI 60) for 135ms
prg32_audio_note(60, 135);
```

## Cartridge AUDIO Block

`.prg32` cartridges may include a trailing AUDIO block after the code payload.
The block is optional; cartridges without it remain valid.

An AUDIO block contains:

- sample descriptors
- instrument descriptors
- track descriptors
- event data
- raw unsigned 8-bit sample bytes

Pack assets:

```bash
python3 tools/prg32audio_pack.py audio.json --out build/audio.block
python3 -m prg32 cartridge build examples/games/asteroids/graphics/game.S \
  --portable \
  --entry-prefix asteroids_graphics \
  --audio-block build/audio.block \
  --out build-esp32c6/asteroids-audio.prg32
```

## Samples

Recommended sample format:

| Property | Value |
|---|---|
| Encoding | unsigned 8-bit PCM |
| Channels | mono |
| Compression | none |
| Base note | required for pitched playback |

Convert WAV files:

```bash
python3 tools/wav2prg32sample.py jump.wav --rate 22050 --normalize --out build/jump.raw
```

## SID-Like Procedural Instruments

Instruments map tracker notes either to PCM sample slots `0..63` or to a
procedural oscillator. A synth ID uses bit 15 as its marker, which cannot
collide with the 64 PCM slots:

```text
bit 15       synth marker (1)
bits 14:12   reserved (currently ignored)
bits 11:10   resonance, 0..3
bits 9:6     low-pass cutoff, 0..15
bits 5:2     pulse width, 0..15
bits 1:0     triangle=0, saw=1, pulse=2, noise=3
```

Use `PRG32_AUDIO_SYNTH_TRI`, `PRG32_AUDIO_SYNTH_SAW`,
`PRG32_AUDIO_SYNTH_PULSE`, or `PRG32_AUDIO_SYNTH_NOISE` rather than assembling
the bits by hand. Pulse-width positions map to duty cycles 1/17 through 16/17,
so neither endpoint can become permanently silent or permanently high.

```c
static const prg32_instrument_desc_t bass = {
    .sample_id = PRG32_AUDIO_SYNTH_PULSE(6, 8, 2),
    .default_volume = 220,
    .default_pan = PRG32_AUDIO_PAN_CENTER,
    .attack = 2,
    .decay = 28,
    .sustain = 180,
    .release = 36,
};
```

The oscillators use a 32-bit phase accumulator. MIDI note 69 is 440 Hz, and
the phase increment is calculated once when a note starts. Noise uses a
deterministic 23-bit LFSR seeded with `0x7ffff8`, taps 22 and 17, zero-state
protection, and one update per oscillator phase wrap. The synth is inspired by
the SID signal path; it is not a cycle-accurate 6581 or 8580 emulator.

Every synth voice applies the existing instrument ADSR bytes. A zero attack or
decay is immediate. Other timing values follow a quadratic mapping from about
1 ms to about 2 seconds. Sustain maps directly from `0..255` to the envelope
level. `prg32_audio_note_off()` begins release for synth voices and retains the
legacy immediate-stop behavior for PCM voices.

The output passes through a bounded fixed-point state-variable low-pass filter.
The 16 cutoff and four resonance settings are musical control positions rather
than calibrated SID frequencies. Extreme state is clamped to keep malformed or
high-resonance combinations stable.

Synth and PCM voices share the same allocator, master/channel volume, pan law,
mono collapse, stereo output, and tracker event format. A tracker `NOTE_ON`
therefore starts a synth whenever its instrument descriptor contains a synth
ID; `NOTE_OFF` releases it. No public function, descriptor, event, or AUDIO
block layout changed.

Procedural oscillators require no waveform bytes in the cartridge. A sustained
one-second PCM tone at 22050 Hz needs 22050 asset bytes, while its synth
instrument needs only the existing eight-byte descriptor. Runtime voice state
and the normal audio buffers still consume RAM, so this is an asset-storage
saving rather than zero-cost audio.

Pulse width, cutoff, and pan are fixed for the life of a note. Automatic LFOs,
ring modulation, and oscillator sync are not currently implemented.

## Tracker Events

Event records are four bytes:

```text
delta_ticks, command, arg0, arg1
```

Initial commands:

| Command | Meaning |
|---|---|
| `NOTE_ON` | `arg0` channel/instrument, `arg1` MIDI note |
| `NOTE_OFF` | `arg0` channel |
| `SET_VOLUME` | `arg0` channel, `arg1` volume |
| `SET_PAN` | `arg0` channel, `arg1` signed pan |
| `SET_TEMPO` | `arg0` BPM |
| `PLAY_SAMPLE` | `arg0` sample id, `arg1` volume |
| `JUMP` | jump to event index from `arg0,arg1` |
| `END` | stop playback |

## Examples

- `examples/features/audio_mono_beep`: generated tone smoke test.
- `examples/features/audio_mono_sample`: C-registered PCM sample.
- `examples/features/audio_mono_tracker`: looping event sequence.
- `examples/features/audio_stereo_pan_test`: left/right/center wiring test.
- `examples/features/audio_stereo_music`: centered melody with panned effects.

## Troubleshooting

No sound:

- verify common ground
- verify VIN, BCLK, LRC/WS, and DIN
- verify Adafruit `SD/MODE` is not tied to GND
- if the speaker emits a steady digital tone, check that DIN is not floating
  because the firmware failed to start I2S
- confirm speaker wires are on speaker outputs
- confirm audio is enabled in Kconfig
- confirm the build uses `sdkconfig.defaults`, not `sdkconfig.defaults.qemu`
- confirm the selected GPIOs do not conflict with display or joystick wiring

Only one stereo speaker works:

- verify both boards receive BCLK, LRCLK, and DATA
- verify both boards have power and ground
- verify each board channel-selection jumper
- run `examples/features/audio_stereo_pan_test`

Left and right reversed:

- swap speaker labels
- swap the channel-selection settings
- update the trainer wiring sheet for that kit

Distorted audio:

- lower master or sample volume
- use a 4-8 ohm speaker
- check for shorted speaker wires
- verify the power supply can drive the speaker current
