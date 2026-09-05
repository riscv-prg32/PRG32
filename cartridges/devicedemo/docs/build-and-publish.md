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

The build scripts use the current unified `python3 -m prg32` command groups:
`cartridge build`, `store attach-metadata`, and `store pack-bundle`.

The source intentionally targets `development-c6`, because indexed/bitplane sprite APIs and procedural synth instrument identifiers are introduced there.
