# Lab 01 - Resident Firmware on PRG32

## Goal

Build, flash, and inspect the resident PRG32 runtime.

## Steps

1. Open `PRG32.code-workspace`.
2. Run task `PRG32: set target esp32c6`.
3. Run task `PRG32: build`.
4. Run task `PRG32: flash monitor`.
5. Confirm the monitor reaches `PRG32 runtime initialized`.
6. Hold A+B while resetting the board and confirm setup mode appears.
7. Run `DEVICE DEMO` from setup.

## Checkpoint

Show the instructor the serial monitor and, if LCD mode is enabled, the display.

## QEMU Option

If no board is available, run:

1. `PRG32: qemu set target esp32c3`
2. `PRG32: qemu screen`

The QEMU monitor should reach the PRG32 runtime, and the virtual screen should
show the 320x240 setup/device-demo display.

## Reflection

Which C function actually writes one character to UART and LCD? Find it and write the file name.
