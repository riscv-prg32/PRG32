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

## Status (reviewed 2026-10-29)

| Application | State |
|---|---|
| PRG32-Construction-Kit | Implemented: Deploy page, **USB device** (`static/js/usb_deploy.js`). |
| CartridgeStore | Implemented: game page, **Install on a PRG32 over USB** (`static/js/usb_install.js`). |

Both load the same client, `static/js/prg32_usb_protocol.js` (client version
`2026-10-29`), byte for byte; each repository's tests compare the two copies
when the repositories are checked out side by side. On 2026-10-29 that module
ran on the physical ESP32-C6 prototype through the Construction-Kit's
`tools/usb_device_check.mjs`: a Kit-style `store` and a Store-served `run`
both passed 8/8 in about 1.7 s. The two pages are tested in Chromium with a
pretend device; a person-picked Web Serial session on the physical console
has not been recorded.

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

The client is `static/js/prg32_usb_protocol.js`, an ES module with no
dependencies kept identical in both web applications. A new application
should copy it rather than re-implement framing. Besides `ProtocolSession` it
exports `choosePort`, `cartridgeInfo`, `cartridgeProblems` and
`describeSerialError`. Its required behavior, which mirrors the Python SDK in
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

Minimal deployment function built on that module (call it from a click
handler):

```js
import { ProtocolSession, cartridgeInfo, cartridgeProblems, choosePort } from './prg32_usb_protocol.js';

export async function deployCartridge(bytes, { slot, command }, onProgress) {
  const session = new ProtocolSession(await choosePort());   // user gesture: first await
  await session.open();                                      // no setSignals(): see above
  try {
    const info = cartridgeInfo(bytes);
    const problems = cartridgeProblems(info, await session.hello(), command);
    if (problems.length) throw new Error(problems.join(' ') + ' Nothing was written.');
    return await session.upload(bytes, { name: info.name, slot, command }, onProgress);
  } finally {
    await session.close();
  }
}
```

The module keeps at most one `reader.read()` outstanding, so bytes that arrive
after a timeout reach the next request, and `close()` cancels the reader
before releasing it so the port is never left locked.

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
progress. Before UPLOAD_BEGIN it checks the console's ABI, maximum size and
`debug_supported` with `cartridgeProblems`, so a refusal never erases a slot.
It also offers **Retrieve performance results**.

| Piece | File |
|---|---|
| Page controller | `static/js/usb_deploy.js` |
| Behaviour tests (Node, pretend device) | `tests/js/usb_protocol.test.mjs`, run by `tests/test_usb_protocol_js.py` |
| Real-browser tests (Chromium) | `tests/test_usb_deploy_browser.py` |
| Physical-console check | `node tools/usb_device_check.mjs PORT CARTRIDGE [slot] [store\|run]` |

The Construction-Kit's [deploy guide](https://github.com/riscv-prg32/PRG32-Construction-Kit/blob/development/docs/deploy.md)
is the student-facing description; this page is the shared technical contract.

## CartridgeStore integration

The CartridgeStore already exposes everything the browser needs:

| Need | API |
|---|---|
| Is there an ESP32-C6 build, and its digest? | `GET /api/games/<id>` → `game.variants.esp32c6.sha256`, `.size` |
| The bytes | `GET /api/games/<id>/download?architecture=esp32c6&version=<v>` |
| Cartridge contract | `game.metadata` (ABI) and the `PRG2` header in the bytes |

The **Install on a PRG32 over USB** panel on `templates/game.html`
(`static/js/usb_install.js`) implements this specification; its user guide
is the Store's `docs/usb_install.md`:

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
6. Serve `static/js/prg32_usb_protocol.js`, the identical copy of the
   Construction-Kit client. Load no CDN code.
7. Exclude the download URL from the service worker cache so a stale cartridge
   is never installed.

The on-device Store browser (setup → BROWSE STORE) is unchanged: it downloads
over Wi-Fi with HTTP and writes the slot with the same stream functions.

## Testing

- **Host unit tests.** The SDK's protocol tests in
  [`tests/test_usb_protocol.py`](../../tests/test_usb_protocol.py) use an
  in-memory fake device for progress, resend, refusal and performance restart.
  The web client has the same tests under Node with
  `tests/js/fake_prg32_serial.js`, a pretend PRG32 behind Web Serial that both
  web applications also inject into Chromium for their page tests.
- **Hardware.** Browsers cannot open QEMU's TCP serial port, so a browser
  deployment must be checked on an ESP32-C6. The Construction-Kit's
  `tools/usb_device_check.mjs` runs the browser module itself on a real port
  under Node, because a browser needs a person to pick the port. Before release, deploy DeviceDemo
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
