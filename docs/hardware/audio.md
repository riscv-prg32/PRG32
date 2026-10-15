# PRG32 Audio Hardware & Configuration

PRG32 uses native ESP-IDF I2S digital audio output connected to external MAX98357A Class-D digital-to-analog converter (DAC) and amplifier breakouts to produce sound.

> [!IMPORTANT]
> **Single Source of Truth for Pin Setups**:
> This component guide describes the audio hardware architecture, signal protocols, and circuit wiring. **Physical GPIO pin numbers are never defined here**.
> All physical pinout tables and wiring assignments are maintained exclusively in the [**Hardware Profiles**](profiles/README.md) directory (see [`prototype1`](profiles/prototype1.md) and [`prototype2`](profiles/prototype2.md)).

---

## Architecture & Overview

PRG32 features an 8-voice procedural synthesizer (SID-style oscillators with ADSR envelopes, noise generator, and multi-mode filters) running at a sample rate of 22,050 Hz with 16-bit PCM resolution. The digital audio stream is piped via the ESP32-C6 hardware I2S peripheral directly to one or two MAX98357A amplifier breakouts driving passive speakers.

- **Audio Protocol**: I2S (Inter-IC Sound) with Philips standard alignment.
- **Sampling Rate**: 22,050 Hz, 16-bit stereo/mono PCM.
- **DAC / Amplifier**: Maxim Integrated MAX98357A mono Class-D amplifier with integrated DAC.
- **Speaker**: Passive 4–8 Ω speaker, 1–3 W (3 W recommended for optimal dynamic range and headroom).

---

## Wiring & Signal Connections

The I2S interface uses three primary digital signals shared across amplifiers:
- **BCLK**: Bit Clock (continuous serial clock for bit timing).
- **LRC / WS**: Left-Right Clock / Word Select (toggles between left and right channels).
- **DIN**: Serial Data Input (transmits the serialized 16-bit PCM audio samples).
- **SD / MODE**: Optional shutdown and channel-selection pin.

For the exact physical GPIO pin assignments on your board, consult the active profile in [Hardware Profiles](profiles/README.md).

### Mono Audio Configuration

In the reference classroom mono build, one MAX98357A breakout drives a single speaker. PRG32 generates audio into duplicated left/right I2S time slots, so a mono breakout will output the full audio signal whether it selects the left slot, right slot, or an average of both.

| Signal Line | MAX98357A Pin | Role / Connection |
|---|---|---|
| **Power** | `VIN` | Connect to 5V rail (or 3.3V; 5V gives maximum 3 W output headroom) |
| **Ground** | `GND` | Common ground rail |
| **Bit Clock** | `BCLK` | Connect to ESP32-C6 I2S BCLK (defined in [Hardware Profiles](profiles/README.md)) |
| **Word Select** | `LRC / WS` | Connect to ESP32-C6 I2S LRC/WS (defined in [Hardware Profiles](profiles/README.md)) |
| **Serial Data** | `DIN` | Connect to ESP32-C6 I2S DIN (defined in [Hardware Profiles](profiles/README.md)) |
| **Mode / Mute** | `SD / MODE` | Leave unconnected for default enabled state (or connect optional shutdown line) |

> [!NOTE]
> On standard Adafruit-style breakouts, leaving `SD / MODE` unconnected defaults to an internal pull-up setting the channel to Left+Right average or Left channel. Do **not** pull `SD` to GND, as any voltage below 0.16 V puts the amplifier into low-power shutdown (mute).

### Stereo Audio Configuration

Stereo audio uses two MAX98357A breakouts sharing the exact same I2S clock and data lines (`BCLK`, `LRC`, and `DIN`). The amplifier channel assignment is configured exclusively by setting the DC bias voltage on the `SD / MODE` pin:

| Signal Line | Left MAX98357A | Right MAX98357A | Notes |
|---|---|---|---|
| **Power** | `VIN` | `VIN` | Shared 5V power bus |
| **Ground** | `GND` | `GND` | Common ground bus |
| **Bit Clock** | `BCLK` | `BCLK` | Shared I2S Bit Clock from MCU |
| **Word Select** | `LRC / WS` | `LRC / WS` | Shared I2S Word Select from MCU |
| **Serial Data** | `DIN` | `DIN` | Shared I2S Serial Data from MCU |
| **Channel Mode** | Tied to `VIN` (> 1.4 V) | 390 kΩ resistor to `VIN` | Left module selects Left channel; right selects Right channel |

#### Channel Mode Voltage Thresholds (`SD / MODE`)

By controlling the voltage applied to `SD / MODE`, the internal state machine determines which stereo audio slot to decode:

| Voltage Range on SD Pin | Mode / Decoded Channel |
|---|---|
| `< 0.16 V` (Tied to GND) | **Shutdown (Mute)** — amplifier is powered down |
| `0.16 V – 0.77 V` | **Left + Right Average** |
| `0.77 V – 1.40 V` | **Right Channel Only** |
| `> 1.40 V` (Tied to VIN) | **Left Channel Only** |

To set the right amplifier module to Right Channel Only when powered from 5V:
- Connect a **390 kΩ (±5%) through-hole axial resistor** between `VIN` (5V) and `SD / MODE` on the right breakout.
- An internal 100 kΩ pull-down resistor inside the MAX98357A forms a voltage divider with the 390 kΩ resistor, producing approximately `5V * (100k / 490k) ≈ 1.02 V`, landing safely within the 0.77 V – 1.4 V window.
- For PCB designs, use a 1206 surface-mount 390 kΩ footprint (see [PCB Reference](../pcb/README.md)).

---

## Passive Buzzer

The firmware contains legacy support for a passive square-wave buzzer. In all modern PRG32 hardware configurations, buzzer support is disabled (`CONFIG_PRG32_PIN_BUZZER = -1`). All synthesized notes, sound effects, and musical tracker events are routed exclusively through the I2S DAC pipeline.

---

## Safety and Power Notes

1. **Speaker Connections Only**: Never connect MAX98357A outputs to headphones, auxiliary jacks, or line inputs of an external amplifier. The MAX98357A uses bridge-tied load (BTL) Class-D outputs where neither speaker terminal is ground. Connecting speaker `-` to ground or line-in will damage the amplifier and host device.
2. **Wire Lengths**: Keep breadboard jumper wires between the ESP32-C6 and the amplifier under 10 cm (4 inches) to minimize high-speed SPI/I2S EMI and ground bounce.
3. **Power Rails**:
   - Power the MAX98357A from the breadboard's lower red 5V bus (`VBUS`).
   - Solderless breadboards should bridge upper and lower ground rails with a heavy gauge jumper so the MCU and audio circuits share an identical ground potential.
4. **Current Capacity**: When driving two 4 Ω speakers at high volume, peak audio currents can exceed 1 A. Ensure the host computer USB port or dedicated USB-C power supply can deliver at least 1.5 A continuous current.

---

## Software Configuration

Audio pin assignments are decoupled from Kconfig defaults and defined in profile fragment files in `profiles/` (see [Hardware Profiles](profiles/README.md)):

```bash
# Verify audio pin assignments for your active hardware profile
python3 -m prg32 esp32c6 pins
```

### Kconfig Options (Custom Profiles Only)

If and only if you are using `Custom Profile` mode in `menuconfig`:
- `CONFIG_PRG32_AUDIO_I2S_BCLK_GPIO`: GPIO number for Bit Clock.
- `CONFIG_PRG32_AUDIO_I2S_LRCLK_GPIO`: GPIO number for Word Select.
- `CONFIG_PRG32_AUDIO_I2S_DIN_GPIO`: GPIO number for Data In.
- `CONFIG_PRG32_AUDIO_I2S_SD_GPIO`: Optional GPIO for hardware shutdown control (`-1` when hardwired).

### QEMU Emulator Emulation

In QEMU emulator builds (`profiles/sdkconfig.defaults.qemu`):
- Physical I2S GPIOs are disabled (`-1`).
- The audio engine serializes the 22,050 Hz PCM stream over a virtual TCP socket (`tcp::4321`) using a credit-based flow control protocol, allowing the host desktop to play sound without hardware emulation (see [QEMU Guide](../usage/qemu.md)).

---

## Related Documentation

- [Hardware Pin Profiles](profiles/README.md): Authoritative pin assignments for `prototype1`, `prototype2`, and custom builds.
- [PRG32 Audio Engine Guide](../tools/audio.md): Sound design, SID oscillator mechanics, and tracker JSON conversion.
- [Hardware Documentation Overview](README.md): Central hardware directory and master breadboard layout.
- [Bill of Materials & Where to Buy](where_to_buy.md): Sourcing MAX98357A breakouts, speakers, and resistors.
