# Firmware Releases

PRG32 publishes the resident ESP32-C6 firmware as
[GitHub releases](https://github.com/riscv-prg32/PRG32/releases). A release can
be flashed without ESP-IDF and without the PRG32 SDK:

- from a terminal, with the stand-alone
  [`tools/prg32_flash_release.py`](../../tools/prg32_flash_release.py);
- from a browser, through the PRG32-Construction-Kit and the CartridgeStore
  (see [web flashing](#web-flashing-construction-kit-and-cartridgestore)).

Flashing a release writes only the bootloader, the partition table, and the
application. Wi-Fi settings, joystick calibration, the default cartridge, the
eight cartridge slots, and scores stay on the board.

## Flash a release from a terminal

You need Python 3.9 or newer and the board's connector labelled **USB**
(see the [setup notes](../../README.md#install--setup) for drivers and serial
permissions). Download
[`prg32_flash_release.py`](../../tools/prg32_flash_release.py), or take it from
the release's assets, and run it:

```bash
python3 prg32_flash_release.py
```

The tool lists the releases, asks for the [pin profile](../hardware/profiles/README.md)
that matches your wiring, downloads the three flash parts, checks each against
the SHA-256 in the manifest, finds the ESP32-C6 native USB port, and flashes.
The first time, it offers to install `esptool` (Espressif's flasher package from
PyPI, which brings `pyserial`) into its own environment at
`~/.prg32/flasher-venv`; nothing is installed into the system Python. Downloads
are cached in `~/.prg32/firmware-cache/<version>/`.

Without questions:

```bash
python3 prg32_flash_release.py --release v1.8.0 --profile breadboard1 --yes
```

| Option | Effect |
|---|---|
| `--list` | List the firmware releases, their ABI, and their profiles |
| `--release TAG` | Flash this release; `latest` is the newest non-pre-release |
| `--profile NAME` | `breadboard1`, `breadboard2`, or `pcb1` |
| `--port PORT` | Serial port, when several boards are connected |
| `--manifest FILE_OR_URL` | Use a `prg32-firmware.json` from a folder or a mirror instead of GitHub (offline classrooms) |
| `--download-only` | Download and verify, do not flash |
| `--dry-run` | Print the `esptool` commands |
| `--erase-all` | Erase the whole flash first. **Deletes Wi-Fi settings, all cartridges, and scores** |
| `--yes` | Answer yes to every question |

Set `GITHUB_TOKEN` if GitHub's limit of 60 anonymous API requests per hour is
reached, for example when a whole classroom shares one address.

> [!WARNING]
> `pcb1` wires the joystick DOWN switch to GPIO13, which is USB D+. After a
> `pcb1` firmware boots, the native USB port stops answering. Flashing it on
> breadboard hardware makes the board unreachable over USB until BOOT and RESET
> are pressed by hand. The tool asks for confirmation before flashing it.

If the screen stays dark after flashing, press RESET once: the chip can stay in
its ROM bootloader after the automatic reset.

## Release contents

Each release has, for a tag such as `v1.8.0`:

```text
prg32-firmware.json                                        manifest (below)
prg32-firmware-v1.8.0-esp32c6-<profile>-bootloader.bin     written at 0x0
prg32-firmware-v1.8.0-esp32c6-<profile>-partition-table.bin written at 0x8000
prg32-firmware-v1.8.0-esp32c6-<profile>-app.bin            written at 0x10000
prg32_flash_release.py                                     the stand-alone flasher
SHA256SUMS                                                 checksums of every file
```

with `<profile>` one of `breadboard1`, `breadboard2`, and `pcb1`.

```text
flash offset   partition          written by a release
0x000000       bootloader         yes
0x008000       partition table    yes
0x009000       nvs                no   (Wi-Fi, calibration, default cartridge)
0x00f000       phy_init           no
0x010000       factory app        yes
0x210000       cart0 ... cart7    no   (8 x 128 KiB cartridge slots)
0x310000       scores             no
```

The parts are separate on purpose. A merged single image fills the gap from
0x9000 to 0xffff with 0xFF and so erases NVS; the
[legacy merged image](publishing_and_flashing_firmware.md) does that.

## Manifest format `prg32-firmware-release-1.0`

`prg32-firmware.json` is the only file a host reads to decide what to flash:

```json
{
  "format": "prg32-firmware-release-1.0",
  "name": "PRG32 resident firmware",
  "version": "v1.8.0",
  "commit": "<git commit>",
  "esp_idf": "v5.4.1",
  "chip": "esp32c6",
  "abi": { "major": 1, "minor": 8, "hash": "0x71b93e23" },
  "flash": { "flash_mode": "dio", "flash_size": "4MB", "flash_freq": "80m" },
  "preserves": ["nvs", "phy_init", "cart0", "...", "cart7", "scores"],
  "profiles": [
    {
      "name": "breadboard1",
      "description": "First breadboard: 5-way digital joystick, buttons A and B",
      "parts": [
        { "role": "bootloader", "offset": "0x0", "file": "...", "size": 21984, "sha256": "..." },
        { "role": "partition-table", "offset": "0x8000", "file": "...", "size": 3072, "sha256": "..." },
        { "role": "app", "offset": "0x10000", "file": "...", "size": 1622352, "sha256": "..." }
      ]
    },
    { "name": "pcb1", "warning": "pcb1 uses GPIO13 (USB D+) ...", "parts": ["..."] }
  ]
}
```

Rules for hosts:

- Reject a `format` other than `prg32-firmware-release-1.0` and a `chip`
  other than `esp32c6`. A later incompatible format gets a new string.
- `file` names are relative to the manifest's own location.
- Verify each part's `sha256` before writing it, and write each part at its
  `offset` only. Never erase or write the partitions in `preserves` unless the
  user explicitly asks for a full erase.
- Show `description` when asking for a profile, and show `warning`, when
  present, before flashing.
- `abi` is the cartridge ABI the firmware exports
  ([ABI](../software/abi.md)); a host can use it to tell whether the user's
  cartridges will still run.

## Making a release

Releases are built by GitHub Actions, not on a developer's computer, so that
local settings such as `profiles/sdkconfig.defaults.local` never reach a
release. [`.github/workflows/release.yml`](../../.github/workflows/release.yml)
runs when a `v*` tag is pushed:

1. For each pin profile, in the `espressif/idf:v5.4.1` container, it builds
   with `profiles/sdkconfig.defaults;profiles/sdkconfig.defaults.esp32c6;profiles/sdkconfig.pins.<profile>`
   (the same defaults as `python3 -m prg32 esp32c6 build -p <profile>`).
2. [`tools/prg32_firmware_release.py`](../../tools/prg32_firmware_release.py) `package`
   copies the three parts and fails if the firmware does not report exactly
   the tag as its version: a `-dirty` suffix means the build changed a tracked
   file.
3. `prg32_firmware_release.py manifest` joins the profiles into
   `prg32-firmware.json` and writes `SHA256SUMS`, and `gh release create`
   publishes them with the flasher.

To release, tag the commit on `main` and push the tag:

```bash
git tag -a v1.8.0 -m "PRG32 firmware v1.8.0"
git push origin v1.8.0
```

The version is taken from `git describe --tags`, so tag before building.
Running the workflow by hand (`workflow_dispatch`) builds the same files as an
artifact without publishing a release.

## Web flashing: Construction Kit and CartridgeStore

The PRG32-Construction-Kit and the CartridgeStore offer the same browsing,
selection, download, and flashing in the browser, using Web Serial and
Espressif's `esptool-js`. They follow the manifest rules above. Because GitHub's
download servers do not allow browser pages on other sites to read release
files, both servers fetch and cache the manifest and the parts and serve them
from their own origin. Their status is tracked in
[Web-to-USB deployment](../software/web_usb_deployment.md#firmware-flashing).

## Validation record

- 2026-11-07: `tests/test_firmware_release.py` checks packaging, the manifest,
  checksums, the offsets never touching NVS, and the flasher's selection and
  command line without network or board. The flasher's dry run from a local
  manifest was checked with no ESP-IDF or PRG32 SDK on the `PATH`.
- 2026-11-07: release `v1.8.0` built by the workflow for all three profiles
  (each reported exactly `v1.8.0`). In a clean environment (`env -i`, a fresh
  `HOME`, `PATH=/usr/bin:/bin`, Apple's Python 3.9), the flasher downloaded from
  the release itself listed `v1.8.0`, installed esptool into its private
  environment, downloaded and verified the `breadboard1` parts, and flashed the
  attached breadboard1 ESP32-C6. esptool erased only 0x0-0x5fff, 0x8000-0x8fff
  and 0x10000-0x19dfff; HELLO then reported firmware `v1.8.0`. No cartridge slot
  was written or erased.
