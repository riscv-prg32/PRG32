# Choosing a PRG32 Board

PRG32 has one source tree and one cartridge ABI. Choose the board for the
lesson or project, then build the resident firmware for that target.

| | ESP32-C6 | ESP32-P4 |
| --- | --- | --- |
| Best for | standard classroom labs | larger games and USB controllers |
| Display | 320x240 SPI ILI9341 | the same 320x240 SPI ILI9341 harness |
| Cartridge executable RAM | 128 KiB | 256 KiB |
| Cartridge package limit | 128 KiB | 512 KiB |
| Controls | GPIO buttons | USB HID gamepad or GPIO buttons |
| Audio | external MAX98357A I2S amplifier | DEV-KIT speaker connector/audio path via I2S |
| Network | native Wi-Fi/BLE | onboard C6 Wi-Fi/BLE coprocessor |

## Builds

```bash
# ESP32-C6
idf.py -B build-esp32c6 -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c6
idf.py -B build-esp32c6 build

# ESP32-P4 with the same ILI9341 wired to the configured GPIO pins
idf.py -B build-esp32p4 -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.p4 set-target esp32p4
idf.py -B build-esp32p4 build
```

QEMU remains the ESP32-C3 virtual-RGB build and is useful for rapid checks.

## ESP32-P4 audio

The P4 chip produces digital I2S audio; it does not directly drive a speaker.
The Waveshare DEV-KIT exposes a connector for an 8 ohm, 2 W speaker and its
board audio path is the P4 alternative to the C6 classroom MAX98357A breakout.
PRG32 keeps the mixer, sample format, voices, and `prg32_audio_*` ABI exactly
the same on both boards.

Before a P4 audio build is used on hardware, set
`CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO`, `CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO`, and
`CONFIG_PRG32_AUDIO_I2S_DATA_GPIO` to the verified pins for the exact DEV-KIT
revision. The checked-in P4 defaults enable mono audio but do not claim a
hardware-validated pin mapping. See [audio.md](audio.md) for the validation
procedure.

## Cartridges

Build portable cartridges with the common RV32 ABI. A cartridge whose memory
image is at most 128 KiB runs on both physical boards when its ABI features
are available. Keep an image at or below 64 KiB when it must also run in the
checked-in QEMU profile. P4-only cartridges may use up to 256 KiB executable
RAM and a 512 KiB package:

```bash
python3 tools/prg32_game.py build game.c --portable --architecture esp32p4 \
  --entry-prefix game --out build-esp32p4/game.prg32
```

The C6 loader intentionally rejects an image that exceeds its 128 KiB window.
Do not use P4-specific hardware directly from cartridge code; add a resident
PRG32 ABI service when a capability should be portable.
