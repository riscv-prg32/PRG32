# Build, device upload, QEMU and Cartridge Store

The scripts follow the same portable ABI-table workflow as the standalone DeviceDemo repository.

```sh
export PRG32_REPO=/path/to/PRG32
export PRG32_ARCHITECTURE=esp32c6
scripts/build.sh
python3 -m prg32 esp32c6 upload dist/devicedemo-esp32c6.prg32 --url http://192.168.4.1

export PRG32_ARCHITECTURE=qemu
scripts/build.sh
scripts/pack-store-bundle.sh
```

The final store bundle is `dist/devicedemo-store-bundle.zip`. Publish it with the PRG32 tooling and your configured Cartridge Store token.

The checked-in catalog media are `assets/screenshot.png` and the directly
downloadable, 30-second audiovisual `assets/preview.mp4`. The root media
validator checks their dimensions, duration, and audio/video tracks in CI. The
preview is recorded from DeviceDemo running in QEMU with
`tools/capture_cartridge_previews.py`; it is not reconstructed from the still
screenshot.

The build scripts use the current unified `python3 -m prg32` command groups:
`cartridge build`, `store attach-metadata`, and `store pack-bundle`.

The source intentionally targets `development-c6`, because indexed/bitplane sprite APIs and procedural synth instrument identifiers are introduced there.

DeviceDemo uses explicit switch dispatch for its page names and draw callbacks.
This keeps local code and string references PC-relative when a portable
cartridge is dynamically loaded at a runtime address different from its link
address; it deliberately avoids initialized absolute-pointer tables.

## Automated builds

`.github/workflows/ci.yml` runs this two-architecture build on pull requests and
pushes to `main` and `development-c6`. It validates both cartridge formats and
the bundle archive, then retains them in the `devicedemo-cartridge-package`
workflow artifact for 14 days. The workflow does not publish to an external
Cartridge Store or require store credentials.
