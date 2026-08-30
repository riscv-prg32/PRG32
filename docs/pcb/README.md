# PRG32 Reference Hardware

## PCB Component Layout

The Fritzing reference board is a 210 x 75 mm landscape PCB. Components are
arranged to keep the game controls accessible on the front and the compute and audio hardware on the rear:

- Front, from left to right: digital joystick, landscape ILI9341 display, A
  button, and B button.
- Rear, from left to right when viewed from the rear: left speaker, left
  MAX98357A, ESP32-C6 development board, right MAX98357A, and right speaker.

The right-channel mode resistor is placed beside the right MAX98357A on the
rear. Through-hole pads remain visible from both PCB sides even when the module
body is mounted on the indicated side.

## Reference Prototype Figures

The following figures are exported from the schematic and breadboard views of
`docs/pcb/PRG32-PCB-0-1.fzz`. They describe the same electrical nets and use
the canonical GPIO assignments.

![PRG32 electrical schematic showing the ESP32-C6, ILI9341 display, joystick, A and B buttons, stereo MAX98357A amplifiers, and speakers](/docs/hardware/images/prg32-electrical-schematic.svg)

*Electrical schematic for the current firmware configuration.*

## Prototype parts

- ESP32-C6 development board
- ILI9341 2.8 inch SPI TFT, 320x240
- one 5-way digital joystick module for player 1
- one MAX98357A I2S DAC/amplifier breakout and one 4-8 ohm speaker for mono
  PRG32 audio
- optional second MAX98357A and speaker for stereo PRG32 Audio Plus
- Jumper wires and breadboard

Desktop QEMU can emulate the PRG32 graphics viewport for early software tests,
but it does not replace this hardware validation. Use the physical board for
LCD wiring, GPIO buttons, I2S output, and Wi-Fi station testing.

The resident firmware also starts the `PRG32` Wi-Fi AP for cartridge uploads.
Keep the antenna area of the ESP32-C6 module clear in the enclosure.

## Multiplayer networking

ESP32-C6 remains the main RISC-V teaching microcontroller and has native Wi-Fi.
Multiplayer cartridges use Wi-Fi station mode to reach a classroom Node.js
WebSocket server; the reference hardware keeps local input to one joystick.

## Reference wiring

For the definitive pin connections, including the display, joystick, and
MAX98357A audio, please see the [Pinouts and Wiring guide in `hardware.md`](/docs/hardware/hardware.md#pinouts-and-wiring).

Stereo uses two MAX98357A boards. Both share BCLK, LRC/WS, DIN, power, and
ground. Configure one board for left-channel output and the other for
right-channel output using the breakout-specific jumper or mode pin.

The default audio GPIOs avoid the reference display and joystick wiring. If a
MAX98357A breakout needs explicit shutdown control, choose an unused SD GPIO
in menuconfig before flashing.

On the Adafruit MAX98357A breakout, `SD` also selects shutdown/channel mode.
Leave it in the breakout's default enabled state for mono, or drive it high from
the optional SD GPIO. Do not connect `SD` directly to GND unless you want the
amplifier shut down. PRG32 duplicates mono samples into both I2S slots, so a
single board can average both slots or select either one.

Do not connect MAX98357A speaker outputs directly to headphones or line-level
inputs. Use 4-8 ohm speakers.

Digirak joystick: <https://www.amazon.it/dp/B07HBPW3DF>.
Wire each joystick direction as a normally-open switch to ground; PRG32 enables
internal pull-ups.

The Fritzing reference design is available as `PRG32-PCB-0-1.fzz` in this
directory.
