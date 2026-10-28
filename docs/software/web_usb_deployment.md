# Web-to-USB Cartridge Deployment

This guide explains how a web application deploys a `.prg32` cartridge to a
PRG32 console through the browser's Web Serial API and the
[`prg32-io-1.0` device protocol](usb_protocol.md). It is the implementation
contract for the two companion web applications:

- [PRG32-Construction-Kit](https://github.com/riscv-prg32/PRG32-Construction-Kit):
  deploys the cartridge a student just compiled;
- [CartridgeStore](https://github.com/riscv-prg32/CartridgeStore): installs a
  published cartridge from the catalog.

Both use the same browser client and the same device behavior. Only where the
cartridge bytes come from, and what is checked before sending them, differs.
The wire format is defined once in [usb_protocol.md](usb_protocol.md); this
page does not repeat it.

## Status (reviewed 2026-10-28)

| Application | State |
|---|---|
| PRG32-Construction-Kit | Implemented in `static/js/prg32_usb_protocol.js` and `static/js/usb_deploy.js` (Deploy page, **USB device**). Follow-ups listed in [Construction-Kit integration](#construction-kit-integration). |
| CartridgeStore | Not implemented yet. [CartridgeStore integration](#cartridgestore-integration) is the specification. |

## How it works

```text
web page (HTTPS)                         PRG32 console (resident firmware)
-----------------                        ---------------------------------
fetch cartridge bytes ──► verify SHA-256, PRG2 header, architecture
navigator.serial.requestPort()  (user gesture, user picks the device)
port.open({baudRate:115200})    ──USB CDC──►  protocol task sees the magic
HELLO                           ◄── capabilities, ABI, limits
UPLOAD_BEGIN {name,slot,size,crc32,command} ──► validate, unload, erase
UPLOAD_DATA (offset, ≤ max_chunk bytes) ×N  ──► write flash, progress bar
UPLOAD_END                      ◄── verified; running or back to setup
port.close()
```

The browser never resets the board and never enters the ROM bootloader; it
talks to the firmware that is already running. Firmware updates remain a
separate, deliberate operation (see
[publishing and flashing firmware](../usage/publishing_and_flashing_firmware.md)).

## Browser requirements

- **Chromium-based browsers** (Chrome, Edge, Opera) on desktop. Firefox and
  Safari do not implement Web Serial; show an explanation instead of the
  button (`'serial' in navigator`).
- **Secure context.** `navigator.serial` exists only on `https://` pages or
  `http://localhost`. A classroom server reached as `http://192.168.x.y:5080`
  is *not* a secure context; serve it through HTTPS (a reverse proxy with a
  certificate) or open the page on the same computer as `localhost`.
- **User gesture.** `requestPort()` must be called from a click handler.
  Previously authorized ports are available from `navigator.serial.getPorts()`
  without a prompt.
- **One owner.** Only one program can open the port. Close `idf.py monitor`,
  PlatformIO Monitor, Arduino Serial Monitor and PRG32 CLI sessions first.

Use a port filter so the chooser lists only ESP32-C6 native USB devices:

```js
const ESPRESSIF_USB_JTAG = { usbVendorId: 0x303a, usbProductId: 0x1001 };
const port = await navigator.serial.requestPort({ filters: [ESPRESSIF_USB_JTAG] });
```

## The browser client

The reference client is the Construction-Kit's
`static/js/prg32_usb_protocol.js` (an ES module with no dependencies). A new
application should reuse it rather than re-implement framing. Its required
behavior, which mirrors the Python SDK in
[`prg32/usb_protocol.py`](../../prg32/usb_protocol.py):

1. **Open without touching the modem lines.** Call
   `port.open({ baudRate: 115200, bufferSize: 32768 })` and do not call
   `setSignals()`. If an application must change DTR/RTS, change both in a
   single `setSignals` call; see
   [modem-control lines](usb_protocol.md#modem-control-lines).
2. **Decode a noisy stream.** Firmware log text shares the connection.
   Search for the 8-byte magic, check version and maximum payload, wait for
   the whole payload, then verify CRC-32. Discard bad frames.
3. **Match replies by request ID** and ignore frames for other IDs, such as a
   late reply to a request that already timed out.
4. **Negotiate.** Send HELLO first. Require `protocol === "prg32-io-1.0"`.
   Use `max_chunk` when present, otherwise 1024 bytes. Check
   `runtime.cart_abi_major`, `cart_max_size` and `cart_ram_size` against the
   cartridge, and refuse `debug` when `debug_supported` is false. All of this
   happens **before** UPLOAD_BEGIN, so a refusal never erases a slot.
5. **Upload in order** and report `received / total` from each STATUS reply.
   If a chunk reply does not arrive within 5 s, send the same chunk once more;
   the firmware acknowledges an identical resend without writing it again.
6. **Finish** with UPLOAD_END and a 30 s timeout. The device holds its result
   screen for 1.2 s before answering.
7. **Read performance results** by sending the first `PERFORMANCE_GET` with
   flag bit 0 set (restart), then repeating until a `RESULT_DATA` frame has
   flag bit 0 set.
8. **Always release** the reader and writer locks and close the port in a
   `finally` block, so a failed deployment does not leave the port busy.

Minimal deployment function built on that module:

```js
import { MESSAGE, ProtocolSession } from './prg32_usb_protocol.js';

export async function deployCartridge(port, bytes, { name, slot, command }, onProgress) {
  const session = new ProtocolSession(port);
  await session.open();                       // no setSignals(): see above
  try {
    const hello = await session.hello();
    if (hello.protocol !== 'prg32-io-1.0') throw new Error('Unsupported PRG32 firmware.');
    if (command === 'debug' && !hello.debug_supported) {
      throw new Error('This console cannot instruction-debug; use PRG32-QT.');
    }
    const limit = hello.runtime && hello.runtime.cart_max_size;
    if (limit && bytes.length > limit) throw new Error(`The cartridge exceeds ${limit} bytes.`);
    return await session.upload(bytes, { name, slot, command }, onProgress);
  } finally {
    await session.close();
  }
}
```

### Errors to present

| Source | Message to show |
|---|---|
| `NotFoundError` from `requestPort` | No device was selected; nothing was written. |
| `NetworkError`/`InvalidStateError` from `open` | The port is in use by another program; close serial monitors. |
| HELLO timeout | The console did not answer: check the cable is a data cable, the board runs PRG32 firmware, and nothing else holds the port. |
| STATUS `invalid upload metadata` | Wrong slot name, size, or command; a bug in the page. |
| STATUS `payload checksum mismatch` | USB data was corrupted; retry, then try another cable. |
| STATUS `upload length or checksum mismatch`, `failed to verify streamed cartridge` | The slot was erased; nothing runs from it. Retry. |
| STATUS `RUN FAILED` reason | The cartridge is stored but did not start; see the console's setup screen. |
| `done` on the reader | The cable was unplugged. The console erases the partial slot after 10 s and returns to setup. |

## Construction-Kit integration

The Deploy page already follows this contract: it downloads the artifact from
`/api/artifacts/<id>/download`, checks the `PRG2` magic, the 128 KiB slot
limit and the SHA-256 recorded at build time, accepts only `esp32c6`
artifacts, asks the student to confirm the slot, and shows the device's
progress. Keep it aligned with this page when either side changes:

- add `{ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] }` to
  `requestPort`;
- use `hello.max_chunk` (fall back to 1024) instead of the fixed 1024-byte
  chunk, and resend a chunk once when its reply times out;
- set flag bit 0 on the first `PERFORMANCE_GET` (`encodeFrame(type, id,
  payload, 1)`) so an interrupted earlier retrieval cannot return the tail of
  an old document;
- keep the no-`setSignals` rule documented in `docs/deploy.md`.

The Construction-Kit's [deploy guide](https://github.com/riscv-prg32/PRG32-Construction-Kit/blob/development/docs/deploy.md)
is the student-facing description; this page is the shared technical contract.

## CartridgeStore integration

The CartridgeStore already exposes everything the browser needs:

| Need | API |
|---|---|
| Is there an ESP32-C6 build, and its digest? | `GET /api/games/<id>` → `game.variants.esp32c6.sha256`, `.size` |
| The bytes | `GET /api/games/<id>/download?architecture=esp32c6&version=<v>` |
| Cartridge contract | `game.metadata` (ABI) and the `PRG2` header in the bytes |

Specification for an **Install on PRG32 (USB)** action on `templates/game.html`:

1. Show the action only when `'serial' in navigator`, `window.isSecureContext`
   and `game.variants.esp32c6` exist; otherwise explain which condition is
   missing.
2. Let the user choose `cart0` to `cart7` and `store` or `run`, and require an
   explicit "replace what is in this slot" confirmation. Do not offer `debug`
   or `performance` in the Store.
3. On click: request the port (step order matters, because the user gesture
   must reach `requestPort`), then download the variant from the same origin,
   compute SHA-256 with `crypto.subtle.digest` and compare it with
   `variants.esp32c6.sha256`, and check the `PRG2` magic and `size`.
4. Deploy with the client above and show `received / total` progress.
5. Count the installation through the normal download endpoint (it already
   calls `record_download`); add no new tracking.
6. Vendor `prg32_usb_protocol.js` into `static/` (copy it from the
   Construction-Kit with its version noted). Load no CDN code.
7. Exclude the download URL from the service worker cache so a stale cartridge
   is never installed.

The on-device Store browser (setup → BROWSE STORE) is unchanged: it downloads
over Wi-Fi with HTTP and writes the slot with the same stream functions.

## Testing

- **Host unit tests.** The SDK's protocol tests in
  [`tests/test_usb_protocol.py`](../../tests/test_usb_protocol.py) use an
  in-memory fake device for progress, resend, refusal and performance restart.
  A web client should have equivalent tests with a mocked `SerialPort`.
- **Hardware.** Browsers cannot open QEMU's TCP serial port, so a browser
  deployment must be checked on an ESP32-C6. Before release, deploy DeviceDemo
  with `store` and `run`, unplug the cable once during a transfer (the console
  must show UPLOAD TIMED OUT within 10 s and return to setup), and confirm that
  merely connecting does not restart a running cartridge.
- **Protocol logic without a browser.** Run the same sequence with the SDK
  against QEMU (`--port socket://127.0.0.1:5555`) or hardware; both use the
  same firmware receiver.

## Constants that must match

| Constant | Value | Defined in |
|---|---|---|
| Magic | `PRG32IO\0` | [usb_protocol.md](usb_protocol.md#frame-header) |
| Protocol string | `prg32-io-1.0` | HELLO response |
| Maximum payload | 16384 bytes | HELLO `max_payload` |
| Maximum chunk | 4096 bytes | HELLO `max_chunk` |
| Slots | `cart0`–`cart7`, 128 KiB each | [`profiles/partitions_prg32.csv`](../../profiles/partitions_prg32.csv) |
| Architecture string | `esp32c6` | [CartridgeStore consistency](../agents/cartridge_store_consistency.md) |
| USB IDs | VID `0x303A`, PID `0x1001` | Espressif USB Serial/JTAG |
