# PRG32 Joystick & Controller Input Hardware

PRG32 provides a unified, register-like input API for 8-bit and 16-bit retro games. This document outlines the physical mechanisms, hardware variants, electrical specifications, and driver behavior for PRG32 controllers.

> [!IMPORTANT]
> **Single Source of Truth for Pin Setups**:
> This component guide describes joystick mechanisms, electrical characteristics, ADC thresholds, and input APIs. **Physical GPIO pin numbers are never defined here**.
> All physical pinout tables and wiring assignments are maintained exclusively in the [**Hardware Profiles**](profiles/README.md) directory (see [`breadboard1`](profiles/breadboard1.md) for the 5-way digital navigation joystick and [`breadboard2`](profiles/breadboard2.md) for the KY-023 analog joystick).

---

## Overview

PRG32 supports two primary hardware joystick architectures on the ESP32-C6:
1. **5-Way Digital Navigation Joystick** (`breadboard1`): Discrete mechanical switches representing Left, Right, Up, Down, and Center Press.
2. **2-Axis Analog Thumbstick** (`breadboard2`): Potentiometer-based analog stick (e.g. KY-023 / PS2 controller thumbstick) sampled with ADC one-shot conversions each time input is read.

Both architectures combine directional input with two tactile momentary pushbuttons (A and B).

---

## 1. Digital 5-Way Navigation Joystick (`breadboard1`)

Used in the reference breadboard design ([Breadboard 1 Profile](profiles/breadboard1.md)).

![Breadboard prototype with digital joystick](images/prg32-breadboard-prototype.svg)

### Mechanical & Electrical Operation
- **Outputs**: Discrete active-low switches for each cardinal direction (Up, Down, Left, Right) and Center Click.
- **Logic Level**: 3.3V logic.
- **Pull-ups**: The ESP32-C6 firmware enables internal pull-ups on all digital input GPIOs. Connecting each switch between its designated MCU GPIO and GND is sufficient; no external pull-up resistors are required.
- **Center Button**: Center click serves as the combined `START / SELECT` button.
- **Pin Assignment**: See [Breadboard 1 Pin Profile](profiles/breadboard1.md).

### Signal Interface (`breadboard1`)

| Input Signal | Function | Electrical Interface |
|---|---|---|
| **LEFT** | Move left direction | Momentary switch shorting pin to GND |
| **RIGHT** | Move right direction | Momentary switch shorting pin to GND |
| **UP** | Move up direction | Momentary switch shorting pin to GND |
| **DOWN** | Move down direction | Momentary switch shorting pin to GND |
| **START / SELECT** | Center click / pause | Momentary switch shorting pin to GND |
| **A** | Primary action / confirm | Normally-open momentary button to GND |
| **B** | Secondary action / back | Normally-open momentary button to GND |

---

## 2. Analog 2-Axis Thumbstick (`breadboard2`)

Used in the upgraded breadboard design ([Breadboard 2 Profile](profiles/breadboard2.md)).

### Mechanical & Electrical Operation
- **Potentiometers**: Dual 10 kΩ orthogonal potentiometers for X and Y displacement (`VRx` and `VRy`).
- **Sampling Backend**: ESP32-C6 ADC one-shot driver on `ADC1`
  (`components/prg32/prg32_joystick_analog.c`). Every input read converts X
  and Y once, about two conversions per frame. `prg32_joystick_analog_get_raw()`
  also converts fresh values, so axis-based input reads the current stick
  position rather than a cached one.
- **Why not continuous DMA**: the ADC continuous driver hands each DMA buffer
  back from an interrupt. Flash erases and writes (cartridge uploads, Store
  downloads, saved settings) switch the flash cache off for longer than the
  DMA needs to fill its buffers, the interrupt cannot run, and the DMA stops
  for good: the stick froze until the next reset. One-shot conversions use no
  DMA and no interrupt, so flash writes cannot stop them.
- **Integrated Push Switch (`SW`)**: Standard tactile switch triggered by pressing the stick inward, serving as `START / SELECT`.
- **Pin Assignment**: See [Breadboard 2 Pin Profile](profiles/breadboard2.md).

> [!WARNING]
> **CRITICAL POWER REQUIREMENT — 3.3V ONLY**:
> Connect the analog joystick `VCC` pin exclusively to the breadboard's **3.3V rail (`3V3`)**, **NEVER to 5V**.
> The ESP32-C6 ADC inputs tolerate a maximum voltage of 3.6V. Powering the potentiometers from 5V will expose the ADC inputs to overvoltage and permanently destroy the ADC peripheral.

### Signal Interface (`breadboard2`)

| Signal Line | Joystick / Button Pin | Electrical Interface |
|---|---|---|
| **Power** | `VCC` | 3.3V rail ONLY |
| **Ground** | `GND` | Common ground rail |
| **X-Axis Analog** | `VRX` | Analog voltage (0V to 3.3V) read by ADC1 Channel (see [profiles](profiles/breadboard2.md)) |
| **Y-Axis Analog** | `VRY` | Analog voltage (0V to 3.3V) read by ADC1 Channel (see [profiles](profiles/breadboard2.md)) |
| **Center Switch** | `SW` | Center push switch shorting pin to GND (`START / SELECT`) |

### ADC Calibration & Thresholds
- **Dynamic Range**: 12-bit ADC (`0` to `4095`).
- **Resting Center**: ~`1650` (ideal midpoint is ~`2048`, but typical KY-023 units idle around 1600–1800).
- **Deadband Hysteresis & Directional Bitmask Mapping**:
  - **Horizontal (X-Axis)**:
    - Raw reading below `CONFIG_PRG32_JOYSTICK_ADC_LEFT_THRESHOLD` (default `1300`): asserts `PRG32_BTN_LEFT`.
    - Raw reading above `CONFIG_PRG32_JOYSTICK_ADC_RIGHT_THRESHOLD` (default `2800`): asserts `PRG32_BTN_RIGHT`.
  - **Vertical (Y-Axis)**:
    - Raw reading below `CONFIG_PRG32_JOYSTICK_ADC_UP_THRESHOLD` (default `1300`): asserts `PRG32_BTN_UP`.
    - Raw reading above `CONFIG_PRG32_JOYSTICK_ADC_DOWN_THRESHOLD` (default `2800`): asserts `PRG32_BTN_DOWN`.
  - **Neutral Deadband**: Readings between low and high thresholds register as neutral (no directional bits set).
  - **Diagonals**: Simultaneous deflection in both axes asserts both directional bits (e.g. `PRG32_BTN_UP | PRG32_BTN_RIGHT`). Axis inversion is also automatically detected if low and high thresholds are swapped.

---

## Action Buttons (A, B, C, D) and Bumpers (L, R)

All PRG32 hardware setups use at least two tactile momentary pushbuttons:
- **Button A (Action / Set)**: Confirms menu selections and performs primary in-game actions.
- **Button B (Back / Reset)**: Cancels menu choices and triggers secondary in-game actions.
- **Buttons C and D** (`breadboard2`): Extra in-game action buttons. The resident
  menus do not use them. Profiles without them leave `CONFIG_PRG32_PIN_BTN_C`
  and `CONFIG_PRG32_PIN_BTN_D` at `-1`, and the bits always read as released.
- **Bumpers L and R** (`breadboard2`): left and right shoulder buttons for
  games. The resident menus do not use them. Profiles without them leave
  `CONFIG_PRG32_PIN_BTN_L` and `CONFIG_PRG32_PIN_BTN_R` at `-1`, and the bits
  always read as released. They are separate from the joystick directions
  `PRG32_BTN_LEFT` and `PRG32_BTN_RIGHT`.
- **Wiring**: Normally-open switches connected directly between the assigned signal GPIO and GND. The internal pull-up holds the pin HIGH until pressed.

---

## System Hardware Shortcuts

PRG32 defines global hardware button combinations handled by resident firmware:

| Button Combination | Timing | Action |
|---|---|---|
| **A + B** (Hold) | During boot / power-on | Force enter Setup Mode (configuration menu) |
| **A + B + DOWN** (Hold) | While cartridge is running | Software reboot back to resident firmware |

---

## Input API Reference

Cartridge code reads inputs as a compact register mask via `prg32_input_read()`:

```c
#include "prg32.h"

uint32_t input = prg32_input_read();

if (input & PRG32_BTN_A) {
    // Button A is held
}
if (input & PRG32_BTN_LEFT) {
    // Left direction is held (either via digital switch or analog tilt)
}
```

### Input Bitmask Constants

| Constant | Bit Value | Action |
|---|---|---|
| `PRG32_BTN_LEFT` | `1 << 0` (0x01) | Move left |
| `PRG32_BTN_RIGHT` | `1 << 1` (0x02) | Move right |
| `PRG32_BTN_UP` | `1 << 2` (0x04) | Move up |
| `PRG32_BTN_DOWN` | `1 << 3` (0x08) | Move down |
| `PRG32_BTN_A` | `1 << 4` (0x10) | Primary action / confirm |
| `PRG32_BTN_B` | `1 << 5` (0x20) | Secondary action / back |
| `PRG32_BTN_SELECT` | `1 << 6` (0x40) | Select / Pause |
| `PRG32_BTN_START` | `1 << 6` (0x40) | Alias for SELECT (center click) |
| `PRG32_BTN_C` | `1 << 7` (0x80) | Extra action button C |
| `PRG32_BTN_D` | `1 << 8` (0x100) | Extra action button D |
| `PRG32_BTN_L` | `1 << 9` (0x200) | Left bumper |
| `PRG32_BTN_R` | `1 << 10` (0x400) | Right bumper |

All buttons fit in `PRG32_INPUT_BUTTONS_MASK` (`0x7ff`). There are no local
player-2 bits: multiplayer is online only.

> [!NOTE]
> Store the mask in a `uint32_t`: `PRG32_BTN_D`, `PRG32_BTN_L` and `PRG32_BTN_R` are bits 8 to 10 and do not fit in a byte. Multiplayer games exchange remote player input frames over native Wi-Fi (see [External Controllers & Multiplayer](external_controllers.md)).

---

## Software Profile Selection

To select the active joystick subsystem for your physical build:

```bash
# Digital 5-way joystick build
python3 -m prg32 esp32c6 build --pin-profile breadboard1

# Analog 2-axis thumbstick build
python3 -m prg32 esp32c6 build --pin-profile breadboard2
```

Or configure via `idf.py menuconfig`:
`Component config` -> `PRG32 Framework` -> `PRG32 Board Configuration` -> `Hardware Pin Profile`.

---

## Related Documentation

- [Hardware Pin Profiles](profiles/README.md): Authoritative pin mappings for `breadboard1` and `breadboard2`.
- [Hardware Documentation Overview](README.md): Master breadboard guide and power rails.
- [External Controllers & Multiplayer](external_controllers.md): Wi-Fi networking and multiplayer input architecture.
- [Bill of Materials & Where to Buy](where_to_buy.md): Sourcing digital and analog joystick components.
