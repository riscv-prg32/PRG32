# PRG32 Onboard RGB LED Hardware & API

PRG32 provides built-in support for controlling WS2812 addressable RGB LEDs (often found on ESP32 development boards such as the ESP32-C6-DevKitC-1) for status indication and audio visualizers.

> [!IMPORTANT]
> **Single Source of Truth for Pin Setups**:
> This component guide describes the WS2812 driver API, DevKit hardware wiring constraints, and driver behavior.
> Console wiring profiles and external pin assignments are maintained exclusively in the [**Hardware Profiles**](profiles/README.md) directory (see [`prototype1`](profiles/prototype1.md) and [`prototype2`](profiles/prototype2.md)).

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

- **Conflict in `prototype1` (Display DC on GPIO8)**:
  In the reference [`prototype1`](profiles/prototype1.md) profile, GPIO8 is allocated to the ILI9341 display's Data/Command (`DC`) line. During SPI rendering, the display driver toggles GPIO8 thousands of times per second. If the WS2812 driver were active on GPIO8 simultaneously:
  - High-speed display toggles cause the onboard LED to flash or emit random colors.
  - WS2812 RMT transmissions interrupt display command sequencing, causing severe graphical artifacts.
  
  To prevent hardware contention:
  - `CONFIG_PRG32_PIN_RGB_LED` defaults to `-1` (disabled) in `profiles/sdkconfig.pins.prototype1`.
  - The resident setup audio menu automatically suppresses the RGB LED visualizer option when the pin is disabled.

- **Unconflicted Harness in `prototype2` (Display DC on GPIO15)**:
  In the [`prototype2`](profiles/prototype2.md) profile, the ILI9341 display harness was redesigned to use GPIO15 for `DC` and GPIO12 for `RST`. This completely frees GPIO8 from display obligations. While `prototype2` defaults `CONFIG_PRG32_PIN_RGB_LED` to `-1` for minimal default builds, developers running `prototype2` on an ESP32-C6-DevKitC-1 can set `CONFIG_PRG32_PIN_RGB_LED=8` in `menuconfig` or their profile fragment to safely use the onboard RGB LED as an audio visualizer without display conflicts.

- **Custom Boards & External NeoPixels**:
  When building custom hardware or attaching external WS2812 addressable LED strips, any unused general-purpose GPIO can be assigned to the driver in Kconfig or profile fragment files.

- **QEMU Emulator**:
  In desktop QEMU builds (`profiles/sdkconfig.defaults.qemu`), physical RGB LED pins are set to `-1` and no physical LED hardware is initialized.

---

## Related Documentation

- [Hardware Pin Profiles](profiles/README.md): Authoritative pin definitions across hardware profiles.
- [Hardware Documentation Overview](README.md): Central hardware directory and master breadboard layout.
- [Display Hardware (ILI9341)](ili9341.md): SPI TFT display architecture.
