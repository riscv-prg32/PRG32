# Cartridge Store Integration

The PRG32 environment provides an open source server called the **Cartridge Store**. 

- Cartridge Store is the companion catalog service for PRG32 cartridges. It publishes architecture-specific `.prg32` artifacts for physical ESP32-C6 boards and QEMU desktop builds.
- It also provides Multiplayer and Score uploading.

Its standalone repository is [riscv-prg32/CartridgeStore](https://github.com/riscv-prg32/CartridgeStore).

This document records the firmware-side integration contract. The current PRG32 repository includes the cartridge metadata format, host tooling, and setup-mode pseudocode. A full embedded browser/downloader can be added once the classroom network policy and memory budget are fixed for the target boards.

<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 620" width="100%" height="100%" style="background-color: #f8f9fa;font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
  <defs>
<!--Core Material Drop Shadow-->
    <filter id="shadow" x="-5%" y="-5%" width="110%" height="110%">
      <feDropShadow dx="0" dy="4" stdDeviation="6" flood-opacity="0.1" flood-color="#000"/>
    </filter>
<!--Primary Arrowhead (Indigo)-->
    <marker id="arrowPrimary" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" fill="#3f51b5"/>
    </marker>
<!--Secondary Arrowhead (Gray)-->
    <marker id="arrowSecondary" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
      <path d="M 0 0 L 14 5 L 0 10 z" fill="#9e9e9e"/>
    </marker>
<!--Clip Path for Cartridge Store Header-->
    <clipPath id="storeClip">
      <rect x="400" y="60" width="280" height="480" rx="12"/>
    </clipPath>
  </defs>
<!--================= CONNECTORS (PATHS) =================-->
<!--Dev to Store-->
  <path d="M 320 155 L 390 155" fill="none" stroke="#3f51b5" stroke-width="2.5" marker-end="url(#arrowPrimary)"/>
<!--Catalog to Consumers (Right Side Flows)-->
<!--1. Catalog to Browse-->
  <path d="M 660 370 L 710 370 Q 720 370 720 360 L 720 330 Q 720 320 730 320 L 935 320" fill="none" stroke="#3f51b5" stroke-width="2.5" stroke-linejoin="round" marker-end="url(#arrowPrimary)"/>
<!--2. Catalog to Device Download-->
  <path d="M 660 420 L 935 420" fill="none" stroke="#3f51b5" stroke-width="2.5" marker-end="url(#arrowPrimary)"/>
<!--3. Catalog to Dev Download-->
  <path d="M 660 470 L 710 470 Q 720 470 720 480 L 720 510 Q 720 520 730 520 L 935 520" fill="none" stroke="#3f51b5" stroke-width="2.5" stroke-linejoin="round" marker-end="url(#arrowPrimary)"/>
<!--================= DEVELOPER MACHINE (PUBLISHER) =================-->
  <g transform="translate(0, 0)">
<!--Base Card-->
    <rect x="30" y="110" width="300" height="90" rx="12" fill="#ffffff" filter="url(#shadow)"/>
<!--Left Accent Border-->
    <rect x="30" y="110" width="8" height="90" rx="4" fill="#3f51b5"/>
<!--Title-->
    <text x="55" y="140" font-weight="600" font-size="16" fill="#202124">Developer Machine</text>
<!--Code Box-->
    <rect x="55" y="152" width="270" height="32" rx="4" fill="#202124"/>
    <text x="65" y="173" font-family="monospace" font-size="13" font-weight="bold" fill="#4caf50">$&gt;</text>
    <text x="90" y="173" font-family="monospace" font-size="13" fill="#e8eaed">python -m prg32 store publish</text>
  </g>
<!--================= CARTRIDGE STORE (CENTER) =================-->
  <g transform="translate(0, 0)">
<!--Base Card-->
    <rect x="400" y="60" width="280" height="480" rx="12" fill="#ffffff" filter="url(#shadow)"/>
<!--Top Header Ribbon-->
    <rect x="400" y="60" width="280" height="6" fill="#3f51b5" clip-path="url(#storeClip)"/>
<!--Store Title-->
    <text x="540" y="100" text-anchor="middle" font-weight="600" font-size="18" fill="#202124">Cartridge Store</text>
<!--Node 1: API Publish Bundle-->
    <rect x="420" y="125" width="240" height="60" rx="8" fill="#f8f9fa" stroke="#e8eaf6" stroke-width="2"/>
<!--POST Badge-->
    <rect x="435" y="144" width="38" height="22" rx="4" fill="#e3f2fd"/>
    <text x="442" y="160" font-size="11" font-weight="700" fill="#1565c0">POST</text>
<!--Endpoint Text-->
    <text x="482" y="160" font-size="13" font-family="monospace" font-weight="600" fill="#3f51b5">/api/publish/bundle</text>
<!--Node 2: Pending Editor Review-->
    <rect x="430" y="220" width="220" height="50" rx="25" fill="#fff8e1" stroke="#ffecb3" stroke-width="1"/>
    <text x="540" y="250" text-anchor="middle" font-size="14" font-weight="600" fill="#f57f17">Pending Editor Review</text>
<!--Node 3: Catalog Database-->
    <rect x="420" y="340" width="240" height="170" rx="8" fill="#3f51b5"/>
<!--DB Styling Header-->
    <path d="M420 348 A8 8 0 0 1 428 340 L652 340 A8 8 0 0 1 660 348 L660 380 L420 380 Z" fill="#303f9f"/>
    <text x="540" y="365" text-anchor="middle" font-size="15" font-weight="600" fill="#ffffff">Catalog</text>
<!--DB Rows Mockup-->
    <rect x="440" y="405" width="200" height="16" rx="4" fill="#5c6bc0"/>
    <rect x="440" y="435" width="160" height="16" rx="4" fill="#5c6bc0"/>
    <rect x="440" y="465" width="180" height="16" rx="4" fill="#5c6bc0"/>
  </g>
<!--Store Internal Flows-->
  <path d="M 540 185 L 540 210" fill="none" stroke="#9e9e9e" stroke-width="2" marker-end="url(#arrowSecondary)"/>
  <path d="M 540 270 L 540 332" fill="none" stroke="#9e9e9e" stroke-width="2" marker-end="url(#arrowSecondary)"/>
<!--================= CONSUMERS (RIGHT) =================-->
  <g transform="translate(0, 0)">
<!--Consumer 1: PRG32 Device (Browse)-->
    <rect x="940" y="285" width="260" height="70" rx="8" fill="#ffffff" filter="url(#shadow)"/>
    <rect x="940" y="285" width="8" height="70" rx="4" fill="#009688"/>
    <text x="965" y="315" font-weight="600" font-size="16" fill="#202124">Browse the Store</text>
    <text x="965" y="335" font-size="13" font-weight="500" fill="#5f6368"> <tspan fill="#009688">PRG32 Device</tspan></text>
<!--Consumer 2: PRG32 Device (Download)-->
    <rect x="940" y="385" width="260" height="70" rx="8" fill="#ffffff" filter="url(#shadow)"/>
    <rect x="940" y="385" width="8" height="70" rx="4" fill="#009688"/>
    <text x="965" y="415" font-weight="600" font-size="16" fill="#202124">Download Cartridges</text>
    <text x="965" y="435" font-size="13" font-weight="500" fill="#5f6368"><tspan fill="#009688">PRG32 Device</tspan></text>
<!--Consumer 3: Developer Machine (Download)-->
    <rect x="940" y="485" width="260" height="70" rx="8" fill="#ffffff" filter="url(#shadow)"/>
    <rect x="940" y="485" width="8" height="70" rx="4" fill="#3f51b5"/>
    <text x="965" y="510" font-weight="600" font-size="16" fill="#202124">Download with Python</text>
    <rect x="965" y="520" width="230" height="24" rx="4" fill="#202124"/>
    <text x="972" y="536" font-family="monospace" font-size="11" font-weight="bold" fill="#4caf50">$&gt;</text>
    <text x="990" y="536" font-family="monospace" font-size="11" fill="#e8eaed">python -m prg32 store download</text>
  </g>
<!--================= API REQUEST LABELS (PILLS) =================-->
<!--Pill 1: GET /api/games-->
  <g transform="translate(800, 290)">
    <rect x="0" y="0" width="120" height="24" rx="12" fill="#e8f5e9" filter="url(#shadow)"/>
    <text x="12" y="16" font-size="12" font-weight="700" fill="#2e7d32">GET</text>
    <text x="40" y="16" font-size="12" font-family="monospace" fill="#37474f">/api/games</text>
  </g>
<!--Pill 2: GET /api/games/<id>/download-->
  <g transform="translate(725, 390)">
    <rect x="0" y="0" width="205" height="24" rx="12" fill="#e8f5e9" filter="url(#shadow)"/>
    <text x="12" y="16" font-size="12" font-weight="700" fill="#2e7d32">GET</text>
    <text x="40" y="16" font-size="12" font-family="monospace" fill="#37474f">/api/games/&lt;id&gt;/download</text>
  </g>
<!--Pill 3: GET /api/games/<id>/download-->
  <g transform="translate(730, 490)">
    <rect x="0" y="0" width="205" height="24" rx="12" fill="#e8f5e9" filter="url(#shadow)"/>
    <text x="12" y="16" font-size="12" font-weight="700" fill="#2e7d32">GET</text>
    <text x="40" y="16" font-size="12" font-family="monospace" fill="#37474f">/api/games/&lt;id&gt;/download</text>
  </g>
</svg>

## Configuration and Connection

### Board Configuration (Setup Menu)

In the Setup menu, you can set up the connection to the cartridge store. The setup page provides manual Cartridge Store URL entry, automatic discovery, and connection testing.

Discovery order:
1. Previously saved Cartridge Store URL from NVS. 
2. Compile-time `CONFIG_PRG32_STORE_URL` flag.
3. mDNS service `_prg32store._tcp.local`.
4. `GET /.well-known/prg32-store.json` on likely local hosts.
5. Manual URL entry in the setup menu.

#### mDNS Auto-discovery
1. Connect the board to the same Wi-Fi network as the store.
2. Enter setup mode.
3. Open `CARTRIDGE STORE`.
4. Select `AUTO-DISCOVER`.
5. When a URL is found, press SELECT to save it.

The store advertises `_prg32store._tcp` on port `5080`.

> **Note**: mDNS is not available in QEMU.

#### Manual IP Entry
1. Enter setup mode.
2. Open `CARTRIDGE STORE`.
3. Select `MANUAL ENTRY`.
4. Enter either a bare IPv4 address such as `192.168.1.42` or a full URL such as `http://192.168.1.42:5080`.
5. Confirm the entry to save it in NVS.

Bare IPv4 addresses are expanded to `http://<address>:5080`.

#### Hardcoding the Store URL
For fixed classroom deployments, you can bake the store URL directly into the firmware. Set `CONFIG_PRG32_STORE_URL` in `menuconfig` or append it to the relevant `sdkconfig.defaults` file before building:

```text
CONFIG_PRG32_STORE_URL="http://192.168.1.42:5080"
```

### Host Tools Configuration (Python)

When using the host-side Python tooling (e.g., publishing or downloading from your PC), you can pass the configuration as CLI flags (`--store-url` and `--token`) or save them in a JSON config file to avoid typing them every time. CLI flags override the JSON config.

Create `~/.prg32/config.json` (or `%USERPROFILE%\.prg32\config.json` on Windows):

```json
{
  "store_url": "http://192.168.1.42:5080",
  "store_token": "replace-with-classroom-token"
}
```

## Downloading and Installing Cartridges

CartridgeStore provides a shared classroom catalog for `.prg32` artifacts. QEMU and physical firmware use different architecture strings (`qemu` and `esp32c6`). The devices can only see and download compatible cartridges.

Two installation paths are available:

### On-Device Installation
After obtaining a connection to a Cartridge Store, you can browse available cartridges, view metadata/colophon, and download a cartridge into one of the available slots (`cart0`, `cart1`, `cart2`, or `cart3`).

1. Enter setup mode.
2. Open `BROWSE STORE`.
3. Scroll the catalog and choose a compatible game.
4. Select a cartridge slot.
5. Download the cartridge.
6. Run it from the Main Menu.

### Host Tool Installation
You can discover stores, list games, and download cartridges using the Python host tools:

```bash
# Discover a store on the LAN
python3 -m prg32 store-discover

# List available games
python3 -m prg32 store-list --store-url http://192.168.1.42:5080

# Download a cartridge and upload it to the board
python3 -m prg32 store-download org.uniparthenope.tetris-c \
  --store-url http://192.168.1.42:5080 \
  --architecture esp32c6 \
  --out build-esp32c6/tetris-c.prg32

python3 -m prg32 upload build-esp32c6/tetris-c.prg32 \
  --url http://192.168.4.1
```

### ABI Validation
Both paths validate the cartridge ABI before deployment. Store downloads are rejected when the cartridge ABI major, ABI hash, required feature bits, import model, or legacy load address are not compatible with the current runtime. Rebuild incompatible cartridges with `--portable` from the matching PRG32 checkout.

## Preparing Cartridges for the Store

A `.prg32` artifact contains one linked cartridge architecture. Build and publish separate artifacts for physical ESP32-C6 hardware and the QEMU graphics workflow:

```bash
# Physical board variant.
python3 -m prg32 cartridge build ... \
  --portable \
  --out build-esp32c6/game.prg32

# QEMU variant.
python3 -m prg32 cartridge build ... \
  --portable \
  --out build-qemu/game.prg32
```

### Store-Ready Metadata
The optional `PRG32META` trailer turns a cartridge into a monolithic store artifact containing metadata, an icon, an optional screenshot, an optional signature, and an optional colophon.

Create the executable cartridge first, then append metadata:

```bash
python3 -m prg32 attach-metadata \
  build-esp32c6/asteroids.prg32 \
  --metadata metadata.json \
  --icon icon.png \
  --screenshot screenshot.png \
  --colophon colophon.json \
  --architecture esp32c6 \
  --out dist/asteroids-esp32c6.prg32
```

Inspect the trailer:

```bash
python3 -m prg32 inspect-metadata dist/asteroids-esp32c6.prg32
```

See [cartridge_metadata.md](cartridge_store.md) for the binary trailer and metadata ABI, [colophon_abi.md](/docs/software/colophon_abi.md) for the colophon ABI, and [cartridge_store.md](cartridge_store.md) for the setup-mode integration contract.

*(Note: The external [DeviceDemo repository](https://github.com/riscv-prg32/DeviceDemo) contains its own cartridge metadata, colophon, Store bundle manifest, and build/publish instructions for the `esp32c6` and `qemu` variants.)*

### Building Portable Examples
Build every checked-in game and feature example as a portable cartridge and prepare flat CartridgeStore bundles:

```bash
python3 tools/prg32_build_portable_examples.py --clean
```
The output directory defaults to `build-portable-examples`. For each example, the script writes a `.prg32` file plus `esp32c6` and `qemu` publishing bundles.

## Publishing to the Cartridge Store

Current Cartridge Store deployments accept zip bundles at `/api/publish/bundle`. Uploads may require a session or Bearer token and are submitted for editor review before they appear in the public catalog.

### Using Python Host Tools

**Build and publish a C cartridge directly:**
```bash
python3 -m prg32 publish \
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

**Pack and publish a multi-architecture bundle:**
```bash
python3 -m prg32 pack-bundle \
  --manifest build-esp32c6/tetris-bundle/manifest.json \
  --out tetris.zip

python3 -m prg32 publish-bundle tetris.zip \
  --store-url http://192.168.1.42:5080
```

### Manual Publishing

Alternatively, you can manually create the metadata bundle and upload it via the API.

**1. Create a bundle directory:**
```bash
mkdir -p build/store/hello_world
cp build-esp32c6/hello_world.prg32 \
  build/store/hello_world/hello_world-esp32c6.prg32
cp build-qemu/hello_world.prg32 \
  build/store/hello_world/hello_world-qemu.prg32
```

**2. Create `build/store/hello_world/manifest.json`:**
```json
{
  "abi": "prg32-metadata-1.0",
  "id": "org.uniparthenope.hello-world",
  "title": "Hello World",
  "version": "1.0.0",
  "summary": "Minimal PRG32 hello world cartridge.",
  "authors": [
    {
      "name": "Your Name",
      "affiliation": "Your Course Or Lab"
    }
  ],
  "tags": ["example", "assembly", "hello-world"],
  "architectures": [
    {
      "id": "esp32c6",
      "file": "hello_world-esp32c6.prg32"
    },
    {
      "id": "qemu",
      "file": "hello_world-qemu.prg32"
    }
  ]
}
```

**3. Package it:**
```bash
cd build/store/hello_world
zip -r ../hello_world-1.0.0.zip manifest.json \
  hello_world-esp32c6.prg32 \
  hello_world-qemu.prg32
cd ../../..
```

**4. Publish with `curl`:**
```bash
curl -X POST "$PRG32_STORE_URL/api/publish/bundle" \
  -H "Authorization: Bearer $PRG32_STORE_TOKEN" \
  -F "bundle=@build/store/hello_world-1.0.0.zip"
```
*(If the store does not require authentication, omit the `Authorization` header.)*

**5. Verify the catalog:**
```bash
curl "$PRG32_STORE_URL/api/games"
curl "$PRG32_STORE_URL/api/games/org.uniparthenope.hello-world"
```
*(If the upload response says `status: pending`, an editor must verify the submission before these catalog requests show the new cartridge.)*

**6. Final smoke test (download & upload):**
```bash
curl "$PRG32_STORE_URL/api/games/org.uniparthenope.hello-world/download?architecture=esp32c6&version=1.0.0" \
  --output build-esp32c6/hello_world_from_store.prg32

python3 -m prg32 upload \
  build-esp32c6/hello_world_from_store.prg32 \
  --url http://192.168.4.1
```

## Store Client API

Firmware clients uses compact REST calls:

| Method | Path                                                          | Purpose                                                                                                                  |
| ------ | ------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `GET`  | `/api/games`                                                  | list games, versions, and available architectures                                                                        |
| `GET`  | `/api/games/<id>`                                             | fetch one game detail record                                                                                             |
| `GET`  | `/api/games/<id>/icon`                                        | fetch icon bytes                                                                                                         |
| `GET`  | `/api/games/<id>/screenshot`                                  | fetch screenshot bytes if available                                                                                      |
| `GET`  | `/api/games/<id>/colophon`                                    | fetch compact colophon JSON                                                                                              |
| `GET`  | `/api/games/<id>/download?architecture=esp32c6&version=1.0.0` | download `.prg32`. The firmware should request `architecture=esp32c6`. QEMU hosts should<br>request `architecture=qemu`. |

## Troubleshooting

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

### Common Missing Tool Fixes

| Symptom | Likely cause | Fix |
|---|---|---|
| `idf.py: command not found` | ESP-IDF shell not exported | Run `. $HOME/esp-idf/export.sh` or use ESP-IDF PowerShell |
| `missing tool: riscv32-esp-elf-gcc` | ESP-IDF toolchain missing or not on `PATH` | Run `./install.sh esp32c3,esp32c6`, then export ESP-IDF |
| `ninja: command not found` | Host build tool missing | Install Ninja with the platform package manager |
| QEMU build cannot find virtual RGB component | Wrong target or defaults | Use `esp32c3` and `sdkconfig.defaults.qemu` |
| Physical display is black | QEMU build flashed to board or wrong pins | Rebuild `build-esp32c6` with `sdkconfig.defaults`; check `main/prg32_config.h` |
| Upload cannot reach board | Host is not on PRG32 Wi-Fi or wrong URL | Connect to `PRG32` AP and use `http://192.168.4.1` |
| Store publish returns `401` | Missing or invalid token | Ask for the classroom token or omit auth only on open stores |
| Store publish returns `400` | Bad manifest or zip layout | Check `manifest.json` and `unzip -l` output |

## TODO
### 3. `GET /.well-known/prg32-store.json` on likely local hosts

**Not implemented for discovery scanning.**

- **In Firmware:** The firmware completely skips this step. It does not scan IP ranges or attempt to `GET /.well-known/prg32-store.json` on fallback addresses if mDNS fails.
- **In Python Tools:** The python tool (`store_discover`) does query `/.well-known/prg32-store.json`, but **only** on hosts it _already discovered_ via mDNS, in order to extract metadata (`name` and `abi`). It does not blindly ping IP addresses to discover them.
- **In Firmware Manual Entry:** The `prg32_store_ping()` function in firmware performs a `GET` to the `.well-known` endpoint to validate ABI compatibility, but it is currently unused in the codebase (not even during manual entry).
