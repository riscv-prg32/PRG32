# PRG32 v2 Hardware Scaffold

This directory is the starting point for the next reference board and enclosure.

## Design Goals

- ESP32-C6 as the main RISC-V teaching CPU.
- ILI9341 SPI display header.
- One 5-way digital joystick module.
- Setup button or joystick push switch for Wi-Fi setup mode.
- Passive buzzer.
- Clear antenna keepout for Wi-Fi station multiplayer tests.
- USB-C power and programming through the development board or carrier.
- Exposed test pads for UART, SPI, reset, boot, and buzzer PWM.

## Multiplayer Topology

```text
ESP32-C6 PRG32 firmware
        |
        v
Wi-Fi station mode
        |
        v
Node.js WebSocket relay on the classroom LAN
```

## Files

- `PRG32_v2.kicad_pro`: KiCad project placeholder.
- `enclosure.scad`: OpenSCAD starter enclosure.

The board is intentionally scaffolded rather than finalized so students can
inspect and extend the design during hardware labs.
