# PRG32 Web API Reference

This document describes the HTTP and discovery APIs used around PRG32:

- the board-local API exposed by PRG32 firmware;
- the external ScoreServer API used for classroom scoreboards;
- the external MetricsServer API used for performance studies;
- the companion CartridgeStore API used to publish, browse, and download
  cartridge bundles.

The APIs are intentionally small and readable. They are suitable for classroom
experiments with `curl`, Python, or simple JavaScript clients.

## Common Conventions

Use plain HTTP on classroom networks unless a reverse proxy adds HTTPS.

| Value | Meaning |
|---|---|
| Board access point URL | `http://192.168.4.1` |
| CartridgeStore default URL | `http://<host>:5080` |
| ScoreServer default URL | `http://<host>:5000` |
| MetricsServer default URL | `http://<host>:8080` |
| JSON content type | `application/json` |
| Cartridge content type | `application/octet-stream` |
| Bundle upload content type | `multipart/form-data` |

Most JSON endpoints return a `2xx` status for success and a `4xx` or `5xx`
status with a short text or JSON body for errors. Firmware endpoints keep error
bodies compact because they run on constrained hardware.

## Board-Local Firmware API

The board API is served by the PRG32 firmware when Wi-Fi support is enabled.
It is used by host tools, labs, and setup workflows.

Enable Wi-Fi in `main/prg32_config.h` or through project configuration:

```c
#define PRG32_WIFI_ENABLE 1
#define PRG32_WIFI_SCORES_ENABLE 1
#define PRG32_WIFI_SSID "your-network"
#define PRG32_WIFI_PASSWORD "your-password"
```

The board can also run as an access point. In that mode the usual URL is:

```text
http://192.168.4.1
```

### List Device Endpoints

```http
GET /api
GET /api/
```

Returns a compact JSON index of the board-local device API. This endpoint is the
first endpoint clients should call when discovering what the running firmware
can serve.

Example:

```bash
curl http://192.168.4.1/api
```

Response:

```json
{
  "ok": true,
  "service": "PRG32",
  "endpoints": [
    {"method":"GET","path":"/api","available":true},
    {"method":"GET","path":"/api/runtime","available":true},
    {"method":"GET","path":"/api/games","available":true},
    {"method":"POST","path":"/api/games","available":true},
    {"method":"POST","path":"/api/games/select","available":true},
    {"method":"GET","path":"/api/screenshot.bmp","available":true},
    {"method":"GET","path":"/api/performance.json","available":true},
    {"method":"GET","path":"/api/scores","available":false},
    {"method":"POST","path":"/api/scores","available":false}
  ]
}
```

Expected behavior:

- `/api` and `/api/` return the same shape;
- endpoints compiled into the firmware are always listed consistently;
- `available:false` means the route exists in the API model but the current
  build/configuration does not serve it, for example score routes when
  `PRG32_WIFI_SCORES_ENABLE` is disabled.

### Get Runtime Information

```http
GET /api/runtime
```

Returns firmware metadata, cartridge ABI information, diagnostic state, the
currently selected cartridge, and import addresses used by the host cartridge
builder.

Example:

```bash
curl http://192.168.4.1/api/runtime
```

Typical response fields:

```json
{
  "name": "PRG32",
  "firmware_version": "1.0.0",
  "cart_magic": "PRG32CART",
  "cart_abi_major": 1,
  "cart_abi_minor": 1,
  "cart_abi_hash": 3117075842,
  "cart_abi_features": 511,
  "cart_load_addr": 1107296256,
  "cart_max_size": 131072,
  "cart_ram_size": 32768,
  "cart_loaded": true,
  "qemu": false,
  "cart": {
    "name": "pong",
    "loaded": true,
    "stored": true,
    "code_size": 12480,
    "mem_size": 2048,
    "audio_size": 0,
    "flags": 0,
    "audio": false,
    "multiplayer": false,
    "generation": 3
  },
  "diag": {
    "frame_count": 1294,
    "input_state": 0
  }
}
```

Expected behavior:

- runtime returns a compact single `application/json` response with firmware,
  cartridge, display-backend, and diagnostic status;
- `cart_abi_hash` and `cart_abi_features` let host tools reject incompatible
  portable cartridges before upload;
- runtime does not include the full cartridge import-address table, because that
  table is too large for a reliable board-local status response while Wi-Fi and
  display services are active;
- `cart_loaded` is `false` when no cartridge is active.
- `qemu` is `true` for QEMU RGB builds and `false` for physical ESP32-C6
  builds.

Main use cases:

- verify that the board is reachable;
- inspect the active cartridge;
- build a cartridge against the exact resident firmware ABI.

### List Cartridge Slots

```http
GET /api/games
```

Returns one object for each cartridge slot.

Example:

```bash
curl http://192.168.4.1/api/games
```

Response:

```json
[
  {
    "slot": "cart0",
    "name": "pong",
    "loaded": true,
    "stored": true,
    "code_size": 12480,
    "mem_size": 2048,
    "audio_size": 0,
    "flags": 0,
    "audio": false,
    "multiplayer": false,
    "generation": 3
  },
  {
    "slot": "cart1",
    "name": "",
    "loaded": false,
    "stored": false,
    "code_size": 0,
    "mem_size": 0,
    "audio_size": 0,
    "flags": 0,
    "audio": false,
    "multiplayer": false,
    "generation": 0
  }
]
```

### Upload A Cartridge

```http
POST /api/games?slot=<slot>
Content-Type: application/octet-stream

<raw .prg32 image>
```

Parameters:

| Parameter | Required | Meaning |
|---|---:|---|
| `slot` | no | Cartridge slot name from `cart0` through `cart3`; defaults to `cart0` |

Example with the host tool:

```bash
python3 -m prg32 upload build/pong.prg32 \
  --url http://192.168.4.1 \
  --slot cart0
```

Example with `curl`:

```bash
curl -X POST 'http://192.168.4.1/api/games?slot=cart0' \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @build/pong.prg32
```

Success response:

```json
{
  "ok": true,
  "slot": "cart0",
  "stored": true,
  "loaded": true,
  "name": "pong",
  "code_size": 12480
}
```

Expected behavior:

- upload is accepted only when `PRG32_GAME_UPLOAD_ENABLE` is enabled;
- validates and stores the cartridge without starting it;
- the request body must fit in the 128 KiB cartridge package limit;
- invalid cartridge images return `400` with the cartridge validation error;
- disabled upload support returns `403`.

Main use cases:

- upload a lab cartridge without reflashing the resident firmware;
- replace a slot during development;
- stage a cartridge downloaded from CartridgeStore.

### Select A Cartridge Slot

```http
POST /api/games/select?slot=<slot>
```

Parameters:

| Parameter | Required | Meaning |
|---|---:|---|
| `slot` | no | Cartridge slot name; defaults to `cart0` |

Example:

```bash
curl -X POST 'http://192.168.4.1/api/games/select?slot=cart1'
```

Success response:

```json
{"ok":true,"slot":"cart1"}
```

Expected behavior:

- the selected slot becomes the active cartridge;
- invalid or empty slots return `400` with the cartridge error message.

### Capture A Screenshot

```http
GET /api/screenshot.bmp
```

Returns the current 320x240 PRG32 framebuffer as a 24-bit BMP image. This
captures the full physical screen, including splash/setup screens, the centered
320x200 game viewport, and the upper/lower status bands.

Example:

```bash
curl http://192.168.4.1/api/screenshot.bmp --output screenshot.bmp
```

Expected behavior:

- the firmware snapshots the current framebuffer without forcing a display
  flush from the HTTP request;
- the response uses `image/bmp`;
- the response includes a fixed `Content-Length`;
- the bitmap is encoded as a conventional 24-bit BMP for broad client
  compatibility;
- the BMP is generated from the same normalized RGB565 framebuffer path used by
  the ILI9341 hardware backend and the QEMU RGB backend;
- the response is marked `Cache-Control: no-store`;
- screenshot transfer is larger than JSON endpoints, so clients should use a
  timeout of at least 30 seconds on weak Wi-Fi links.

Main use cases:

- collect screenshots for lab reports;
- inspect QEMU or hardware output from a script;
- compare visual regressions in simple tests.

### Download Performance Results

```http
GET /api/performance.json
```

Returns the latest setup-mode performance test result. The endpoint streams JSON
chunks so the firmware does not need to allocate a second full copy of the data.

Example:

```bash
curl http://192.168.4.1/api/performance.json \
  --output prg32_performance.json
```

Expected behavior:

- returns the most recent in-RAM setup performance test;
- rebooting the board or QEMU clears the stored result;
- read [Metrics API](/docs/measurement/metrics_api.md) for the full JSON field reference.

## Score API

Please refer to the [Score API](/docs/cartridge_store/score_api.md) for full documentation on local and ScoreServer leaderboards.

## CartridgeStore Discovery API

CartridgeStore is the companion catalog service for PRG32 cartridges:

```text
https://github.com/riscv-prg32/CartridgeStore
```

Canonical discovery constants:

| Constant | Value |
|---|---|
| mDNS service type | `_prg32store._tcp` |
| mDNS default port | `5080` |
| Discovery ABI | `prg32-store-discovery-1.0` |

### mDNS Discovery

CartridgeStore advertises `_prg32store._tcp.local` on port `5080` by default.
Physical ESP32-C6 firmware can discover this service on the local network.
QEMU builds cannot use mDNS through the virtual network and should use a
configured URL.

Host-side example:

```bash
python3 -m prg32 store-discover
```

### Well-Known Discovery Document

```http
GET /.well-known/prg32-store.json
```

Response:

```json
{
  "abi": "prg32-store-discovery-1.0",
  "name": "PRG32 Cartridge Store",
  "api": "http://192.168.1.42:5080/api",
  "web": "http://192.168.1.42:5080/",
  "version": "1.0.0"
}
```

Expected behavior:

- clients must verify the `abi` value before treating a server as compatible;
- firmware setup saves the chosen base URL in NVS;
- runtime URL priority is saved NVS value, then `CONFIG_PRG32_STORE_URL`, then
  mDNS discovery.

## CartridgeStore Catalog API

The store API works with architecture-specific cartridge artifacts.

Canonical architecture strings:

| Target | Architecture |
|---|---|
| Physical ESP32-C6 board | `esp32c6` |
| QEMU desktop build | `qemu` |

### List Games

```http
GET /api/games
```

Example:

```bash
python3 -m prg32 store-list \
  --store-url http://192.168.1.42:5080
```

Typical response shape:

```json
{
  "games": [
    {
      "id": "org.uniparthenope.tetris-c",
      "title": "tetris-c",
      "version": "1.0.0",
      "summary": "Tetris for PRG32",
      "tags": ["game", "c"],
      "architectures": ["esp32c6", "qemu"]
    }
  ]
}
```

Client note: PRG32 host tooling accepts a top-level array or an object with
`games`, `items`, or `cartridges`.

### Get Game Details

```http
GET /api/games/<id>
```

Returns one game detail record, including metadata, versions, assets, and
available architectures. Use this before showing a detailed setup-page preview.

Example:

```bash
curl http://192.168.1.42:5080/api/games/org.uniparthenope.tetris-c
```

### Download Assets

```http
GET /api/games/<id>/icon
GET /api/games/<id>/screenshot
GET /api/games/<id>/colophon
```

Expected behavior:

- `icon` returns compact image bytes when an icon was published;
- `screenshot` returns image bytes when a screenshot is available;
- `colophon` returns compact JSON using the `prg32-colophon-1.0` ABI.

These endpoints are optional for minimal classroom stores. Clients should handle
`404` by showing a simple text-only game record.

### Download A Cartridge

```http
GET /api/games/<id>/download?architecture=<architecture>&version=<version>
```

Parameters:

| Parameter | Required | Meaning |
|---|---:|---|
| `architecture` | yes | `esp32c6` or `qemu` |
| `version` | no | Requested semantic version; store default is usually latest |

Example:

```bash
python3 -m prg32 store-download org.uniparthenope.tetris-c \
  --store-url http://192.168.1.42:5080 \
  --architecture esp32c6 \
  --out build-esp32c6/tetris-c.prg32
```

Equivalent `curl`:

```bash
curl 'http://192.168.1.42:5080/api/games/org.uniparthenope.tetris-c/download?architecture=esp32c6&version=1.0.0' \
  --output tetris-c.prg32
```

Expected behavior:

- response body is the raw `.prg32` cartridge image;
- incompatible or missing architectures return `404` or another documented
  store-side error;
- downloaded physical cartridges should be uploaded to the board with
  `POST /api/games`.

## CartridgeStore Publishing API

Publishing endpoints are used by `python3 -m prg32`. Stores may require a
Bearer token.

Default host config:

```json
{
  "store_url": "http://192.168.1.42:5080",
  "store_token": "my-api-token"
}
```

Save it as:

```text
~/.prg32/config.json
```

Command-line `--store-url` and `--token` values take precedence.

### Publish A Bundle

```http
POST /api/publish/bundle
Authorization: Bearer <token>
Content-Type: multipart/form-data

bundle=<zip file>
```

The zip bundle contains:

```text
manifest.json
<architecture cartridge>.prg32
<optional icon/splash/colophon assets>
```

Manifest example:

```json
{
  "abi": "prg32-metadata-1.0",
  "id": "org.uniparthenope.tetris-c",
  "title": "tetris-c",
  "version": "1.0.0",
  "summary": "Tetris for PRG32",
  "tags": ["game", "c"],
  "assets": {
    "icon": "icon.png",
    "splash": "splash.png"
  },
  "architectures": [
    {"id": "esp32c6", "file": "tetris-c-esp32c6.prg32"}
  ]
}
```

Tool example:

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

Expected behavior:

- missing or invalid tokens commonly return `401`;
- invalid bundles return `400`;
- successful responses are JSON and normally create a pending submission;
- the game appears in the public catalog after an editor verifies it.

### Publish A Prebuilt Bundle

```http
POST /api/publish/bundle
Authorization: Bearer <token>
Content-Type: multipart/form-data

bundle=<zip file>
```

Tool example:

```bash
python3 -m prg32 pack-bundle \
  --manifest build-esp32c6/tetris-bundle/manifest.json \
  --out tetris.zip

python3 -m prg32 publish-bundle tetris.zip \
  --store-url http://192.168.1.42:5080
```

Use this endpoint when the build artifacts already exist or when publishing a
multi-architecture bundle.

`POST /api/publish` remains a compatibility alias for the same zip-bundle
upload shape. The Cartridge Store no longer accepts the old loose multipart
`.prg32` upload fields.

## MetricsServer API

Please refer to [Metrics API](/docs/measurement/metrics_api.md) for the full MetricsServer and performance API documentation.

## End-To-End Workflows

### Upload A Local Cartridge To A Board

```bash
python3 -m prg32 cartridge build examples/games/pong/c/game.c \
  --portable \
  --entry-prefix pong_c \
  --out build-esp32c6/pong.prg32

python3 -m prg32 upload build-esp32c6/pong.prg32 \
  --url http://192.168.4.1 \
  --slot cart0
```

### Publish Then Install From CartridgeStore

```bash
python3 -m prg32 publish \
  examples/games/tetris/c/game.c \
  --portable \
  --entry-prefix tetris_c \
  --name tetris-c \
  --id org.uniparthenope.tetris-c \
  --version 1.0.0 \
  --architecture esp32c6 \
  --store-url http://192.168.1.42:5080

python3 -m prg32 store-download org.uniparthenope.tetris-c \
  --store-url http://192.168.1.42:5080 \
  --architecture esp32c6 \
  --out build-esp32c6/tetris-c.prg32

python3 -m prg32 upload build-esp32c6/tetris-c.prg32 \
  --url http://192.168.4.1 \
  --slot cart0
```

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `404` on a board endpoint | Wi-Fi API not enabled or wrong base URL | Check firmware config and board IP |
| `403` on `POST /api/games` | Upload support disabled | Enable `PRG32_GAME_UPLOAD_ENABLE` |
| `400` during upload | Invalid image, oversized image, or bad slot | Rebuild the cartridge and check `slot` |
| Empty board score list after reflashing with an erased score partition | Local score partition was erased | Re-submit scores or sync from the classroom service |
| Store discovery finds nothing | mDNS blocked or QEMU build | Enter the store URL manually |
| Store download missing architecture | Only another target was published | Publish `esp32c6` or `qemu` as needed |
| `401` on publish | Missing or invalid Bearer token | Add `--token` or `store_token` |
| No metrics appear on server | Metrics disabled or server unreachable | Check metrics Kconfig and server URL |


## Related Documentation

- `docs/cartridges.md`: cartridge upload workflow.
- `docs/cartridge_store.md`: CartridgeStore user workflow.
- `docs/setup_mode_cartridge_store.md`: firmware setup-mode integration notes.
- `/docs/cartridge_store/../cartridge_store/score_api.md`: focused score API guide.
- `//docs/measurement/../measurement/metrics_api.md`: performance and metrics field reference.
- `docs/cartridge_metadata.md`: metadata and colophon formats.
