# PRG32 Reference Hardware

## PCB Component Layout

The Fritzing reference board is a 210 x 75 mm landscape PCB. Components are
arranged to keep the game controls accessible on the front and the compute and audio hardware on the rear:

- Front, from left to right: digital joystick, landscape ILI9341 display, A
  button, and B button.
- Rear, from left to right when viewed from the rear: left speaker, left
  MAX98357A, ESP32-C6 development board, right MAX98357A, and right speaker.

The **390 kΩ** right-channel mode resistor is placed beside the right
MAX98357A on the rear. The Fritzing PCB view uses a 1206 resistor footprint;
the breadboard view depicts the equivalent axial resistor. Through-hole pads
remain visible from both PCB sides even when the module body is mounted on the
indicated side.

The PCB exposes the optional stereo circuit, while the shipped firmware
profiles default to mono (`CONFIG_PRG32_AUDIO_MODE_MONO=y`). A one-amplifier
assembly therefore matches the default firmware. A two-amplifier assembly must
select `CONFIG_PRG32_AUDIO_MODE_STEREO=y` before the firmware is built.

## Layout Status

Reviewed **2026-10-28** against the canonical `breadboard1` pins in
[`profiles/sdkconfig.pins.breadboard1`](../../../profiles/sdkconfig.pins.breadboard1).

| Net | ESP32-C6 pin | Schematic | PCB copper |
|---|---|---|---|
| LCD D/C | GPIO8 | connected | routed on the top layer |
| P1 DOWN | GPIO0 | connected | routed (top and bottom layers, four vias) |
| LCD RESET | GPIO9 | connected | **not routed**: shown as a ratsnest line |
| — | GPIO12, GPIO13 | unconnected | unconnected (native USB D-/D+) |

Before this review the project file drew LCD RESET, LCD D/C and DOWN on
GPIO9, GPIO8 and GPIO13 in the schematic while its connection records and PCB
copper used GPIO0, GPIO1 and GPIO13. The schematic, its connection records,
and the breadboard figure now agree with the firmware. The D/C and DOWN traces
were routed with 24 mil width and checked for at least 10 mil clearance (the
Fritzing default keepout) against all other copper on both layers.

Two layout tasks remain before this board can be fabricated:

1. **Route LCD RESET to GPIO9.** The LCD header pin is enclosed on both
   layers by the display and ESP32-C6 headers and the traces between them, and
   GPIO9 sits on the far ESP32-C6 header. Fritzing's autorouter or a manual
   re-route of neighbouring nets is needed.
2. **Separate the right MAX98357A from the LCD header.** One plated pad of the
   right MAX98357A footprint overlaps LCD header pins 4 (RESET) and 5 (D/C).
   This overlap predates the pin review and would short those signals; move
   the module before routing RESET.

Run Fritzing's **Routing → Design Rules Check** after both changes.

## Reference Prototype Figures

The electrical schematic is exported from the schematic view of
`docs/hardware/pcb/PRG32-PCB-0-1.fzz` (Fritzing `-svg` batch export). The
[breadboard figure](../hardware.md) comes from a separate breadboard sketch
that is not stored in the repository, so it is maintained by editing its SVG
wires directly. Both use the canonical GPIO assignments.

![PRG32 electrical schematic showing the ESP32-C6, ILI9341 display, joystick, A and B buttons, stereo MAX98357A amplifiers, and speakers](/docs/hardware/images/prg32-electrical-schematic.svg)

*Electrical schematic for the current firmware configuration.*

## Prototype parts

The [bill of materials and purchasing guide](../where_to_buy.md) covers
breadboard builds only. Soldered PCB boards are documented by their pin
profile, such as [`pcb1`](../profiles/pcb1.md), and have no purchasing list.

Desktop QEMU can emulate the PRG32 graphics viewport for early software tests,
but it does not replace this hardware validation. Use the physical board for
LCD wiring, GPIO buttons, I2S output, and Wi-Fi station testing.

The shipped physical firmware profile selects Wi-Fi AP boot mode with SSID
`PRG32`, and cartridge upload support is enabled by default. Keep the antenna
area of the ESP32-C6 module clear in the enclosure. QEMU disables physical
Wi-Fi transport in `main/prg32_config.h`.

## Multiplayer networking

ESP32-C6 remains the main RISC-V teaching microcontroller and has native Wi-Fi.
Multiplayer cartridges use Wi-Fi station mode to reach a classroom Node.js
WebSocket server; the reference hardware keeps local input to one joystick.

## Reference wiring

For the definitive pin connections, including the display, joystick, and
MAX98357A audio, please see the [Hardware Profiles Guide](/docs/hardware/profiles/README.md) and [Hardware Guide](/docs/hardware/README.md).

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

Joystick purchasing options are maintained in the
[central buying guide](../where_to_buy.md).
Wire each joystick direction as a normally-open switch to ground; PRG32 enables
internal pull-ups.

The Fritzing reference design is available as `PRG32-PCB-0-1.fzz` in this
directory.
