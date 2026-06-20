# PRG32 Audio

PRG32 audio is a small retro-style digital audio runtime for classroom games.
It keeps the first steps as simple as a buzzer command, while giving students a
path toward PCM samples, tracker-like music, and stereo panning.

The setup menu includes an audio page that auto-detects the currently usable
output path: none, PWM buzzer, mono I2S, or stereo I2S. Trainers can adjust the
test volume, play a short tune, and enable the onboard RGB LED as a
spectrum-style VU meter when the LED GPIO is available.

## Overview

The audio stack has four pieces:

```text
PRG32 program
|-- sample, note, and track API calls
|-- AUDIO cartridge block assets
|-- signed 16-bit mono/stereo mixer
`-- ESP-IDF I2S output to MAX98357A amplifier boards
```

The ESP32-P4 DEV-KIT follows the same I2S contract, but routes the output to
its speaker connector/audio path rather than the C6 reference MAX98357A
breakout. This is a resident-firmware choice: portable cartridges keep using
the same `prg32_audio_*` functions and AUDIO blocks.

The runtime uses unsigned 8-bit mono samples as source assets and mixes them as
signed 16-bit PCM internally. Integer and fixed-point arithmetic keep the real
time path teachable and avoid floating point inside the mixer.

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

### ESP32-P4-Module DEV-KIT speaker

The Waveshare ESP32-P4-Module DEV-KIT provides a connector for an 8 ohm, 2 W
speaker. The P4 emits I2S digital audio; it is not safe to connect a passive
speaker to GPIO pins. Configure the board's verified BCLK, LRCLK/WS, and DATA
pins in `sdkconfig.defaults.p4` or menuconfig before flashing. Keep the mode
mono unless the board audio path has been validated for separate channels.

The resident P4 runtime must expose the same mono/stereo availability through
`prg32_audio_get_mode()`. Therefore existing C and assembly cartridges need no
audio changes. Until the pin mapping is verified on the exact board revision,
the P4 audio defaults are configuration placeholders rather than a tested
wiring recipe.

Required for mono:

- one MAX98357A board
- one 4-8 ohm speaker, 1-3 W

Optional for stereo:

- two MAX98357A boards
- two 4-8 ohm speakers, 1-3 W

Do not connect the MAX98357A speaker outputs directly to headphones or line-in
inputs. They are bridged speaker outputs.

## MAX98357A Wiring

Default PRG32 audio Kconfig pins:

| ESP32-C6 | MAX98357A |
|---|---|
| 3V3 or 5V | VIN |
| GND | GND |
| GPIO4 | BCLK |
| GPIO11 | LRC / WS |
| GPIO23 | DIN |
| not wired by default | SD / MODE, optional |
| GND or default | GAIN |

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
builds intentionally disable I2S output.

Stereo wiring shares the same I2S signals:

| ESP32-C6 | Left MAX98357A | Right MAX98357A |
|---|---|---|
| 3V3 or 5V | VIN | VIN |
| GND | GND | GND |
| GPIO4 / BCLK | BCLK | BCLK |
| GPIO11 / LRCLK | LRC / WS | LRC / WS |
| GPIO23 / DATA | DIN | DIN |
| optional SD GPIO | SD | SD |

Configure the left board for the left I2S channel and the right board for the
right channel. Breakout labels vary: common names include `L/R`, `GAIN`,
`SD_MODE`, `SD`, and `MODE`. Check the exact board pinout before soldering.

## Bill Of Materials

Base PRG32:

| Quantity | Item | Purpose |
|---:|---|---|
| 1 | ESP32-C6 development board | PRG32 host |
| 1 | PRG32-supported display | video output |
| 1 | digital joystick module | player 1 input |

Mono audio:

| Quantity | Item | Purpose |
|---:|---|---|
| 1 | MAX98357A I2S DAC/amplifier breakout | mono output |
| 1 | 4-8 ohm speaker, 1-3 W | mono speaker |
| 5-6 | jumper wires | I2S and power |
| 1 | optional JST speaker connector | detachable speaker |

Stereo audio:

| Quantity | Item | Purpose |
|---:|---|---|
| 2 | MAX98357A breakouts | left and right output |
| 2 | 4-8 ohm speakers, 1-3 W | stereo speakers |
| 8-10 | jumper wires | shared I2S and power |
| 2 | optional JST speaker connectors | detachable speakers |

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

RGB LED helpers:

- `prg32_rgb_led_init(gpio)`: initialize the board LED on a free GPIO.
- `prg32_rgb_led_set(red, green, blue)`: set the LED color.
- `prg32_rgb_led_vu(level)`: map a 0-255 level to blue/green/yellow/red.

Pitch uses `1024` as the natural sample speed. Volumes use `0..255`.

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
python3 tools/prg32_game.py build examples/games/asteroids/graphics/game.S \
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

## Instruments

Instruments map tracker notes to samples. The first implementation stores ADSR
fields for future lessons, but playback currently uses sample id, default
volume, and default pan.

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
