# AGENTS.md

This cartridge tracks the PRG32 `main` branch. Before changing APIs, read the corresponding PRG32 documentation and public headers. Keep code and documentation in sync. Prefer compact indexed assets over RGB565 arrays when appropriate. Preserve the portable ABI-table build. Do not introduce firmware-private calls. Before completion run `git diff --check` when in a git tree, `python3 -m prg32 doctor` when the CLI is available, and build at least one of ESP32-C6 or QEMU. Keep DeviceDemo safe: diagnostic pages must not reconfigure WiFi, erase cartridges, or publish network state automatically.

When making changes to this cartridge, run the following local checks and package scripts from the repository root:

```bash
cartridges/devicedemo/tests/check_source.sh
PRG32_REPO="$PWD" PRG32_ARCHITECTURE=esp32c6 cartridges/devicedemo/scripts/build.sh
PRG32_REPO="$PWD" PRG32_ARCHITECTURE=qemu cartridges/devicedemo/scripts/build.sh
PRG32_REPO="$PWD" cartridges/devicedemo/scripts/pack-store-bundle.sh
```
