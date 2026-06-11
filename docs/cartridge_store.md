# CartridgeStore Integration

## 1. Overview

```text
Developer machine
  prg32_game.py publish -------------> Cartridge Store /api/publish/bundle
                                                    |
                                                    v
                                           pending editor review
                                                    |
                                                    v
                                                catalog
PRG32 device (Setup -> BROWSE STORE)  <-- GET /api/games
PRG32 device (Setup -> download)      <-- GET /api/games/<id>/download
prg32_game.py store-download          <-- GET /api/games/<id>/download
```

Cartridge Store is the companion catalog service for PRG32 cartridges. It
publishes architecture-specific `.prg32` artifacts for physical ESP32-C6 boards
and QEMU desktop builds.

## 2. Connecting a board to a CartridgeStore

### mDNS auto-discovery

1. Connect the board to the same Wi-Fi network as the store.
2. Enter setup mode.
3. Open `CARTRIDGE STORE`.
4. Select `AUTO-DISCOVER`.
5. When a URL is found, press SELECT to save it.

The store advertises `_prg32store._tcp` on port `5080`.

### Manual IP entry

1. Enter setup mode.
2. Open `CARTRIDGE STORE`.
3. Select `MANUAL ENTRY`.
4. Enter either a bare IPv4 address such as `192.168.1.42` or a full URL such
   as `http://192.168.1.42:5080`.
5. Confirm the entry to save it in NVS.

Bare IPv4 addresses are expanded to `http://<address>:5080`.

### Kconfig bake-in

For fixed classroom deployments, set `CONFIG_PRG32_STORE_URL` in menuconfig or
in the relevant `sdkconfig.defaults` file:

```text
CONFIG_PRG32_STORE_URL="http://192.168.1.42:5080"
```

Runtime priority is NVS value, then Kconfig value, then mDNS discovery.

## 3. Browsing and installing from the board

1. Enter setup mode.
2. Open `BROWSE STORE`.
3. Scroll the catalog and choose a compatible game.
4. Select a cartridge slot.
5. Download the cartridge.
6. Choose `Run now` after installation, or return to setup.

QEMU and physical firmware use different architecture strings. Publish the
matching `qemu` or `esp32c6` variant before expecting it to appear as
compatible.

The host tools and firmware validate the cartridge ABI before deployment. Store
downloads are rejected when the cartridge ABI major, ABI hash, required feature
bits, import model, or legacy load address are not compatible with the current
runtime. Rebuild incompatible cartridges with `--portable` from the matching
PRG32 checkout.

To prepare all checked-in examples for store publishing:

```bash
python3 tools/prg32_build_portable_examples.py --clean
```

The generated zip files under `build-portable-examples` are ready for
`tools/prg32_game.py publish-bundle` or manual CartridgeStore upload.

## 4. Publishing from the developer machine

```bash
# Discover a store on the LAN
python3 tools/prg32_game.py store-discover

# List available games
python3 tools/prg32_game.py store-list --store-url http://192.168.1.42:5080
```

Build and publish a C cartridge directly:

```bash
python3 tools/prg32_game.py publish \
  examples/games/tetris/c/game.c \
  --portable \
  --entry-prefix tetris_c \
  --name tetris-c \
  --id org.uniparthenope.tetris-c \
  --version 1.0.0 \
  --summary "Tetris for PRG32" \
  --architecture esp32c6 \
  --store-url http://192.168.1.42:5080
```

Current Cartridge Store deployments accept zip bundles at
`/api/publish/bundle`. Uploads may require a session or Bearer token and are
submitted for editor review before they appear in the public catalog.

Pack and publish a multi-architecture bundle:

```bash
python3 tools/prg32_game.py pack-bundle \
  --manifest build-esp32c6/tetris-bundle/manifest.json \
  --out tetris.zip

python3 tools/prg32_game.py publish-bundle tetris.zip \
  --store-url http://192.168.1.42:5080
```

The PRG32 hardware smoke test is published from the external
[DeviceDemo repository](https://github.com/riscv-prg32/DeviceDemo). That
repository contains its own cartridge metadata, colophon, Store bundle manifest,
and build/publish instructions for the `esp32c6` and `qemu` variants.

Download a cartridge from the host and upload it to the board:

```bash
python3 tools/prg32_game.py store-download org.uniparthenope.tetris-c \
  --store-url http://192.168.1.42:5080 \
  --architecture esp32c6 \
  --out build-esp32c6/tetris-c.prg32

python3 tools/prg32_game.py upload build-esp32c6/tetris-c.prg32 \
  --url http://192.168.4.1
```

Default store credentials can be kept in `~/.prg32/config.json`:

```json
{
  "store_url": "http://192.168.1.42:5080",
  "store_token": "my-api-token"
}
```

Command-line `--store-url` and `--token` values always take precedence.

## 5. QEMU notes

mDNS is not available in the QEMU virtual network. Use `CONFIG_PRG32_STORE_URL`
for QEMU builds, or write the URL to NVS from firmware setup. Host-side
`prg32_game.py store-discover` still works from the developer machine.

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `CONFIGURE STORE FIRST` in `BROWSE STORE` | No store URL configured | Use `CARTRIDGE STORE` -> `AUTO-DISCOVER` or `MANUAL ENTRY` |
| `UNAVAILABLE` in `BROWSE STORE` | Wi-Fi not connected or store down | Check Wi-Fi and ping the store from a PC |
| `NOT FOUND` after `AUTO-DISCOVER` | mDNS unreachable across Wi-Fi | Use `MANUAL ENTRY` with the store IP address |
| `NOT COMPATIBLE WITH THIS FIRMWARE` | No matching architecture in catalog | Publish the matching architecture variant first |
| `TOO LARGE` during download | Cartridge exceeds slot partition | Re-flash with a larger partition, or use a smaller cartridge |
| `401` from `prg32_game.py publish` | Missing or invalid API token | Add `--token` or set `store_token` in `~/.prg32/config.json` |
| Published game is not visible | Upload is pending editor review | Ask an editor to verify the submission in Cartridge Store |
| QEMU build shows `NOT FOUND` for mDNS | Expected: mDNS is unavailable in QEMU | Set `CONFIG_PRG32_STORE_URL` in `sdkconfig.defaults.qemu` |
