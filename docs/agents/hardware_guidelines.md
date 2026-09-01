# Hardware Guidelines

Hardware docs should reinforce the architecture:

ESP32-C6 is the main RISC-V teaching MCU. Do not document it as a general USB HID
host for arbitrary gamepads. Multiplayer uses the ESP32-C6 native Wi-Fi radio.

- When hardware or wiring configurations change, always update `docs/hardware.md`
  to match the new canonical configuration in `main/prg32_config.h`. `docs/hardware.md`
  must remain the single authoritative documentation for pinouts and wiring.
