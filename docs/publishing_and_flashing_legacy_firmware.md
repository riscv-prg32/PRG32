# Publishing And Flashing Legacy Firmware

This workflow publishes the resident ESP32-C6 firmware as one merged binary.
It is useful for a classroom or release archive where students should flash one
file instead of tracking the bootloader, partition table, and app offsets.

## Prepare The Single File

Build and merge the physical firmware:

```bash
python3 -m prg32 esp32c6 prepare-legacy
```

The script runs the ESP-IDF build for `build-esp32c6`, reads
`build-esp32c6/flasher_args.json`, and writes:

```text
publish/legacy-firmware/
|-- PRG32-legacy-esp32c6.bin
|-- PRG32-legacy-esp32c6.json
`-- flasher_args.json
```

Use `--skip-build` only when `build-esp32c6/flasher_args.json` already belongs
to the exact firmware you want to publish:

```bash
python3 -m prg32 esp32c6 prepare-legacy --skip-build
```

Checkpoint: keep the `.bin` and `.json` together. The JSON records the target,
flash settings, source files, and the `0x0` write offset used by the flasher.

## Flash The Published File

Connect the ESP32-C6 board and flash the published image:

```bash
python3 -m prg32 esp32c6 flash-legacy \
  publish/legacy-firmware/PRG32-legacy-esp32c6.json \
  --port /dev/cu.usbmodem5ABA0099241
```

On Linux the port is usually similar to `/dev/ttyACM0`. On Windows it is usually
similar to `COM5`.

After flashing, reset the board and hold A+B during boot to enter setup mode.

## Notes

- This is a resident firmware workflow, not a cartridge workflow.
- Portable `.prg32` cartridges remain the preferred game distribution format.
- Legacy absolute-import cartridges must be rebuilt for the exact resident
  firmware image. When possible, build cartridges with `--portable`.
- If `python3 -m esptool` is missing, source ESP-IDF first or install the
  ESP-IDF Python environment used for the build.
