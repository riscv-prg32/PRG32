# Build and Environment Verification

This guide explains how to verify that your PRG32 development environment is set up correctly and how to smoke-test your hardware setup using the `DeviceDemo` cartridge.

## Checking the Environment with the Doctor Tool

The Python tooling includes a `doctor` command that runs a series of sanity checks on your environment. This is especially useful if you are experiencing build issues.

Run the doctor command in your terminal from the root of the project:

```bash
python3 -m prg32 doctor
```

The `doctor` tool verifies:
- Whether the **ESP-IDF** toolchain is properly loaded (e.g., `idf.py` is in your `PATH`).
- Whether the **RISC-V compiler** (`riscv32-esp-elf-gcc`) is available.
- Whether the **partitions file** (`partitions_prg32.csv`) exists and can be parsed correctly for cartridge slots.

If any of these checks fail, ensure you have sourced the ESP-IDF export script (e.g., `. ~/esp-idf/export.sh`) before attempting to build the project.

## Hardware Verification Cartridges

Once your environment is working and the firmware is built and flashed, the easiest way to verify that your physical PRG32 hardware is functioning correctly is to run external diagnostic cartridges.

### DeviceDemo

The [DeviceDemo cartridge](https://github.com/riscv-prg32/DeviceDemo) is a comprehensive test suite designed to run through the various capabilities of the PRG32 framework. It allows you to quickly smoke-test:

- **Display and Graphics:** Sprites, scrolling, playfields, and viewport rendering.
- **Advanced Graphics:** Fixed-point raycaster, tile-engine platformer, dual-playfield space cockpit, and arcade-inspired sketches.
- **Input:** Button mapping and responsiveness.
- **Audio:** Buzzer tones and audio VU behavior.
- **Hardware Integration:** RGB LED functionality, Wi-Fi status, cartridge state, and status bands.

### InputTester

The [InputTester cartridge](https://github.com/riscv-prg32/InputTester) is a focused tool specifically for debugging and verifying hardware inputs. It provides a visual readout of:

- D-Pad directions and action buttons (A, B, Select, Start)
- GPIO button mapping accuracy
- Potential hardware bouncing or ghosting issues
- Button presses make an audio sound that is notified with text. You can verify if audio is working.

To verify your setup using these cartridges: you can clone the repositories or download them directly from the Cartridge Store if connection is available.

If the tests in these cartridges behave as expected, your board's wiring and the PRG32 framework are working correctly!

## Development Guide

> [!IMPORTANT]
> This information is only intended for developers of the PRG32 framework.

### Device Demo Details

- Keep the external `riscv-prg32/DeviceDemo` cartridge current whenever
  cartridge-facing framework capabilities are added or changed. It should
  remain a quick hardware/classroom smoke test covering display, input, audio,
  Wi-Fi status, cartridge state, sprites, scrolling/playfields, status bands,
  RGB LED/audio VU behavior, arcade-inspired viewport sketches, the tile-engine
  platformer, the fixed-point raycaster, the dual-playfield space cockpit, and
  any new framework feature.
