# PRG32 Onboard RGB LED Hardware & API

PRG32 provides built-in support for controlling WS2812 addressable RGB LEDs (often found on ESP32 development boards such as the ESP32-C6-DevKitC-1) for status indication and audio visualizers.

> [!IMPORTANT]
> **Single Source of Truth for Pin Setups**:
> This component guide describes the WS2812 driver API, DevKit hardware wiring constraints, and driver behavior.
> Console wiring profiles and external pin assignments are maintained exclusively in the [**Hardware Profiles**](profiles/README.md) directory.

---

## Overview

The PRG32 RGB LED subsystem uses the ESP32-C6 RMT (Remote Control) peripheral to drive single-wire WS2812/NeoPixel timing protocols without CPU cycle-counting.

### API Functions

The C framework exposes two functions declared in `prg32.h`:

```c
#include "prg32.h"

// Initialize the RGB LED driver on an unconflicted GPIO pin
void prg32_rgb_led_init(int gpio);

// Set 8-bit RGB color channels (0–255 each)
void prg32_rgb_led_set(uint8_t red, uint8_t green, uint8_t blue);
```

---

## Hardware Constraint: ESP32-C6 Onboard LED on GPIO8

On official Espressif development boards (such as the **ESP32-C6-DevKitC-1-N8**):
- The onboard addressable WS2812 RGB LED is **physically hardwired on the PCB to GPIO8** with 3.3V power.
- Because this trace connects the ESP32-C6 chip directly to the onboard LED on the board itself, any external circuit connected to the header pin for GPIO8 will share this electrical node with the WS2812 data input.

### Peripheral Pin Conflicts & Profile Solutions

Because the onboard LED occupies GPIO8 on the DevKit, hardware pin profiles must account for this shared resource:

- **Conflict in `breadboard1` (Display DC on GPIO8)**:
  In the reference [`breadboard1`](profiles/breadboard1.md) profile, GPIO8 is allocated to the ILI9341 display's Data/Command (`DC`) line. During SPI rendering, the display driver toggles GPIO8 thousands of times per second. If the WS2812 driver were active on GPIO8 simultaneously:
  - High-speed display toggles cause the onboard LED to flash or emit random colors.
  - WS2812 RMT transmissions interrupt display command sequencing, causing severe graphical artifacts.

  To prevent hardware contention:
  - `CONFIG_PRG32_PIN_RGB_LED` defaults to `-1` (disabled) in `profiles/sdkconfig.pins.breadboard1`.
  - The resident setup audio menu automatically suppresses the RGB LED visualizer option when the pin is disabled.
  - `CONFIG_PRG32_PIN_SHARED_RGB_LED=8` tells the firmware where the shared LED is, so it can still give a boot indication (see [Shared Boot Indication](#shared-boot-indication)).

- **Same conflict in `pcb1`**:
  The soldered [`pcb1`](profiles/pcb1.md) board also puts LCD D/C on GPIO8, so it uses the same settings as `breadboard1`: `CONFIG_PRG32_PIN_RGB_LED=-1` and `CONFIG_PRG32_PIN_SHARED_RGB_LED=8`.

- **Unconflicted Harness in `breadboard2` (Display DC on GPIO10, Audio LRCLK on GPIO15)**:
  In the [`breadboard2`](profiles/breadboard2.md) profile, the ILI9341 display harness uses GPIO9 for `CS` and GPIO10 for `DC`, with `RST` wired to the ESP32-C6 EN pin (and GPIO15 assigned to I2S LRCLK). This completely frees GPIO8 from display obligations. While `breadboard2` defaults `CONFIG_PRG32_PIN_RGB_LED` to `-1` for minimal default builds, developers running `breadboard2` on an ESP32-C6-DevKitC-1 can set `CONFIG_PRG32_PIN_RGB_LED=8` in `menuconfig` or their profile fragment to safely use the onboard RGB LED as an audio visualizer without display conflicts.

- **Custom Boards & External NeoPixels**:
  When building custom hardware or attaching external WS2812 addressable LED strips, any unused general-purpose GPIO can be assigned to the driver in Kconfig or profile fragment files.

- **QEMU Emulator**:
  In desktop QEMU builds (`profiles/sdkconfig.defaults.qemu`), physical RGB LED pins are set to `-1` and no physical LED hardware is initialized.

### Shared Boot Indication

Two settings describe the onboard LED:

| Setting | Meaning |
|---|---|
| `CONFIG_PRG32_PIN_RGB_LED` | LED owned by the public API and the audio visualizer. The driver refuses any pin used by the display, input, or buzzer. |
| `CONFIG_PRG32_PIN_SHARED_RGB_LED` | LED whose data input is electrically tied to LCD D/C. Only the firmware drives it, and only between display transfers. |

Set the shared pin only when the LED and LCD D/C are on one wire, and keep `CONFIG_PRG32_PIN_RGB_LED=-1` for that pin. When it is set, the firmware-internal helpers in `components/prg32/prg32_led_internal.h`:

1. flash the LED cyan three times early in `prg32_init()`, before the display is initialized (`prg32_shared_rgb_led_boot_flash()`);
2. send an off frame at the end of every `prg32_gfx_present()`, wait for the WS2812 latch gap, and drive the line high again for the display (`prg32_shared_rgb_led_off()`).

The display keeps toggling D/C during transfers, which the WS2812 can read as data, so brief green or white flashes are still expected. Software cannot make both devices independently controllable on one wire; a board that needs a usable LED must give it its own GPIO, as [`breadboard2`](profiles/breadboard2.md) does.

### Cost of the Off Frame

The off frame is sent inside `prg32_gfx_present()` on every frame that changes
the screen, and creates and removes an RMT channel each time. Measured on
`breadboard1` (then named `prototype1`) on 2026-10-31 with the reference
[performance suite](../performance_test.md) (600 frames, ESP-IDF v5.4, two runs
per firmware; mean over all workloads):

| Firmware | Frame time | `present_us` | FPS | RMT errors |
|---|---|---|---|---|
| Before: LED wait timed out at once | 82.0-82.5 ms | 46.5-47.0 ms | 12.11-12.19 | about 25 per second |
| Current | 81.0 ms | 45.6 ms | 12.34 | none |
| Test build without the off frame | 80.2 ms | 44.8 ms | 12.47 | none |

So the off frame costs about 0.75 ms per frame, and the rest of `present_us` is
the LCD update itself. The display, not the LED, sets the frame rate.

---

## Related Documentation

- [Hardware Pin Profiles](profiles/README.md): Authoritative pin definitions across hardware profiles.
- [Hardware Documentation Overview](README.md): Central hardware directory and master breadboard layout.
- [Display Hardware (ILI9341)](ili9341.md): SPI TFT display architecture.
