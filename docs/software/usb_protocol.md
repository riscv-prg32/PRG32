# PRG32 Device Protocol

`prg32-io-1.0` is the binary development protocol shared by ESP32-C6 native
USB, QEMU UART-over-TCP, and the loopback-only PRG32-Qt development listener.
It lets the SDK, the PRG32-Construction-Kit and the CartridgeStore upload, run,
debug where supported, press controller keys, and retrieve performance results
without Wi-Fi. The
running firmware recognizes the first valid frame; there is no upload mode and
no reset into the ROM bootloader.

Implementations:

| Side | File |
|---|---|
| Firmware receiver | [`components/prg32/prg32_usb_protocol.c`](../../components/prg32/prg32_usb_protocol.c) |
| Python SDK client | [`prg32/usb_protocol.py`](../../prg32/usb_protocol.py) |
| Browser client guide | [Web-to-USB cartridge deployment](web_usb_deployment.md) |

## Transport and framing

- ESP32-C6: USB Serial/JTAG CDC on the board's native USB connector. The baud
  rate is ignored by the hardware; hosts conventionally open it at 115200.
- QEMU: UART 0 TCP server, commonly `socket://127.0.0.1:5555`.
- PRG32-Qt: `socket://127.0.0.1:8081`; the listener is deliberately loopback
  only because the protocol has no authentication.

The firmware console shares the line with protocol frames on ESP32-C6 and
QEMU, so hosts must search for the frame magic and ignore text between frames.
The firmware never lets console text land inside a frame: it routes the
console through the same driver as the protocol
(`usb_serial_jtag_vfs_use_driver()` or `uart_vfs_dev_use_driver()`) and writes
each reply frame, header and payload, in one driver call. A reply frame is at
most 1,024 bytes, the size of the ESP32-C6 transmit ring, so RESULT_DATA
carries at most 768 bytes of performance JSON per frame; hosts request chunks
until the final flag, as before.

### Modem-control lines

The ESP32-C6 USB Serial/JTAG peripheral resets the chip when the host asserts
RTS while DTR is released. Operating systems and browsers assert **both** DTR
and RTS when a port opens, which is harmless, and release both together when it
closes, which is also harmless. Changing the two lines one after the other
passes through the reset state. A host must therefore:

- leave DTR and RTS exactly as the operating system set them at open;
- never request `dtr=False`/`rts=False` separately (pyserial clears DTR first,
  which restarted the firmware on every connection before October 2026);
- if a line state must be changed, change both in one call, for example Web
  Serial `port.setSignals({dataTerminalReady: false, requestToSend: false})`.

USB-UART development boards with the classic two-transistor auto-reset circuit
follow the same rule.

### Frame header

Each frame begins with this packed, little-endian 24-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `PRG32IO\0` magic |
| 8 | 1 | version (`1`) |
| 9 | 1 | message type |
| 10 | 2 | flags |
| 12 | 4 | request ID |
| 16 | 4 | payload size, at most 16384 |
| 20 | 4 | IEEE CRC-32 of the payload (the `zlib.crc32` polynomial) |

The magic makes decoding self-synchronizing when firmware logs share the
stream. Every request receives a response with the same request ID. Status
responses use compact UTF-8 JSON with `ok` and either `message` or `error`.
A frame whose payload CRC does not match is answered with
`{"ok":false,"error":"payload checksum mismatch"}` so the host fails
immediately instead of waiting for its timeout. A frame whose bytes stop
arriving for one second is abandoned and the receiver searches for the next
magic.

Flag bit 0 marks the final chunk of a response. On a `PERFORMANCE_GET`
request the same bit asks the firmware to restart the result document from the
beginning; hosts set it on their first request.

On QEMU, bytes outside frames are the emulator keyboard and are forwarded to
the keyboard decoder, including a partial magic that turns out not to be a
frame.

## Messages

| Type | Name | Payload |
|---:|---|---|
| `0x01` | HELLO | empty/JSON; returns capabilities and runtime contract |
| `0x02` | UPLOAD_BEGIN | metadata JSON |
| `0x03` | UPLOAD_DATA | `uint32 offset` followed by 1 to 4096 bytes |
| `0x04` | UPLOAD_END | optional JSON; verifies size and CRC |
| `0x05` | RUN | JSON containing `slot` |
| `0x06` | DEBUG | JSON containing `slot`; PRG32-Qt only |
| `0x07` | PERFORMANCE_GET | empty; returns result data |
| `0x08` | WIFI_SCAN | empty; returns nearby and stored networks ([Wi-Fi](#wi-fi-messages)) |
| `0x09` | WIFI_SET | JSON with `ssid` and `password`; stores and joins a network |
| `0x0A` | WIFI_FORGET | JSON with `ssid`; erases a stored network |
| `0x0B` | KEY | JSON with `action`, `keys`, `hold_ms`; presses or releases controller keys ([Key messages](#key-messages)) |
| `0x80` | STATUS | response JSON |
| `0x81` | RESULT_DATA | performance JSON; flag bit 0 marks the final chunk |

### HELLO response

```json
{"ok":true,"protocol":"prg32-io-1.0",
 "capabilities":["upload","run","performance","keys"],
 "keys":["LEFT","RIGHT","UP","DOWN","A","B","START"],"debug_supported":false,
 "firmware":"5202077","target":"esp32c6","slots":8,
 "max_chunk":4096,"max_payload":16384,
 "runtime":{"cart_ram_size":131072,"cart_max_size":131072,
            "cart_abi_major":1,"cart_abi_minor":1,
            "heap_free":120220,"heap_largest":94208}}
```

`firmware`, `target`, `slots`, `max_chunk`, `max_payload` and
`runtime.cart_abi_minor` were added in October 2026. Hosts must treat missing
fields as unknown and fall back to 1024-byte chunks. `target` is the IDF target
(`esp32c3` for the QEMU build). `capabilities` contains `"wifi"` only when the
firmware was built with Wi-Fi (not on QEMU); hosts must check it before
sending a [Wi-Fi message](#wi-fi-messages). `capabilities` contains
`"keys"` when the firmware accepts the [KEY message](#key-messages), and the
`keys` field, added with it, lists the key names that firmware accepts. `runtime.heap_free` and
`runtime.heap_largest` (free 8-bit heap and its largest block, in bytes, at
the time of the reply) were added on 2026-10-29 for memory measurements; see
[Maximizing free heap](../hardware/memory.md#maximizing-free-heap).

### Upload sequence

UPLOAD_BEGIN metadata contains `name`, `slot` (`cart0` to `cart7`), `size`,
`crc32`, and `command`. The command is `store`, `run`, `debug`, or
`performance`; any other value is rejected. Firmware validates the metadata
**before** erasing anything, then:

1. pauses the main loop and setup menu, unloads the running cartridge, and
   shows the upload screen;
2. erases only the sectors the image needs plus one guard sector (a 24 KiB
   cartridge erases 28 KiB instead of the whole 128 KiB slot);
3. accepts ordered UPLOAD_DATA chunks, writing each to flash and updating the
   running CRC. A chunk that repeats the previous one exactly (a host resending
   after a lost reply) is acknowledged without being written twice; any other
   offset is rejected as out of order;
4. on UPLOAD_END checks length and CRC, validates the stored cartridge
   (header, sizes, ABI, and payload CRC, read through a flash mapping), and
   for `run`/`performance` selects it.

Every STATUS reply to UPLOAD_DATA reports `received` and `total` bytes.
UPLOAD_END returns `name`, `slot`, `size`, and `running`.

### Wi-Fi messages

Added on 2026-10-30. All three reply with STATUS JSON and are refused with
`upload in progress` during an upload, and with
`Wi-Fi is not available in this build` on firmware without Wi-Fi. The firmware
side is described in [stored networks](../usage/network.md#stored-networks).

**WIFI_SCAN** scans for about 3 to 5 seconds. A connected board scans without
dropping its connection; otherwise the radio restarts for the scan and the
saved mode comes back afterwards.

```json
{"ok":true,
 "networks":[{"ssid":"LabNet","rssi":-51,"channel":9,"auth":"WPA2","known":true}],
 "known":["LabNet"],"connected":"LabNet","ip":"192.168.1.20","truncated":false}
```

`networks` holds one entry per SSID (its strongest access point), strongest
first, hidden networks left out, at most 16. Weaker entries are removed until
the reply fits one 1,024-byte frame, and `truncated` is then `true`. `known`
lists the stored network names, newest first; passwords are never sent.

**WIFI_SET** `{"ssid":"LabNet","password":"..."}` stores the network and joins
it. The SSID is 1 to 31 characters; the password is empty (open network) or 8
to 63 characters. The firmware waits up to 20 s for an address. On success it
replies with `connected`, `ip`, and `known`, and the network becomes the first
stored one. On failure it restarts the previous Wi-Fi configuration and replies
`{"ok":false,"error":"<reason>"}`, for example `NO AP FOUND`, `AUTH FAILED`,
or `connect timeout`; nothing is stored. Hosts should allow 60 s for the reply.

**WIFI_FORGET** `{"ssid":"LabNet"}` erases the network and its password and
replies with `known`, `connected`, and `ip`; an SSID that is not stored is
answered with `network is not stored`. Forgetting the network in use switches
to the next stored network, or to access-point mode when none is left.

### Key messages

Added on 2026-10-30. **KEY** lets a developer or a test press the controller
buttons of a running device, with the same effect as the physical buttons on
ESP32-C6 and the keyboard on QEMU: cartridges and the setup menu read remote
keys through `prg32_controller_read()`, joined with the other inputs, and see
no difference.

```json
{"action":"press","keys":["UP","A"],"hold_ms":200}
```

| Field | Meaning |
|---|---|
| `action` | `press` (the default when missing) or `release` |
| `keys` | array of key names from HELLO `keys`. `SELECT` is accepted as a second name for `START`, which is one button ([input](framework_manual.md#input)). Several keys are pressed together |
| `hold_ms` | how long a press lasts, 1 to 60000; 200 when missing |

Every press is released by the firmware, so a host that disappears cannot leave
a key stuck for more than 60 s. `release` ends a press early; a `release` with
missing or empty `keys` releases all remote keys. To hold a key for an
unknown time, send `press` with a long `hold_ms`, then `release`.

The reply is STATUS JSON listing the remote keys still down, which for a press
includes the new keys: the reply does not wait for the release.

```json
{"ok":true,"active":["UP","A"]}
```

A game polls input once per frame and a frame can outlast a short press, so a
key stays down until `hold_ms` has passed **and** two controller reads have
returned it (the main loop reads once per frame, just before the cartridge's
update). A press is therefore never lost between two frames, and a press sent
while nothing reads input is delivered to the next reader. To send a sequence so that the game also sees
each release, wait at least one frame after `hold_ms` and at least two frames in
all before the next request; a press sent while the same key is still down
extends it and is not a new press. `prg32 esp32c6 key` waits `--hold-ms` plus
`--gap-ms`, which by default is 300 ms in all.

Errors, each with no key changed: `invalid key request`, `invalid key action`,
`keys must be an array`, `unknown key <name>` (one unknown name refuses the
whole request), `missing keys` (a press with none), `invalid hold_ms`, and
`upload in progress` (the main loop is paused, so nothing would read the key).

Remote keys are released when an upload begins and on every `RUN`, so a key does
not reach the next cartridge. A+B+DOWN is the
[restart hotkey](framework_manual.md#input) whatever the source of the keys: a
host that presses the three together restarts the firmware.

Hosts check for the `keys` capability first; firmware without it replies
`unsupported protocol message`. Adding a button is described in
[device protocol consistency](../agents/device_protocol_consistency.md#adding-a-controller-key).

## Device behavior

The upload screen shows the cartridge name, target slot, byte percentage, and
progress bar; it is redrawn at most every 200 ms because a full ILI9341 frame
costs about 150 ms. Completion is held for 1.2 s so the result is readable.

| Outcome | Screen | Slot afterwards | Next screen |
|---|---|---|---|
| `store` succeeded | UPLOAD SUCCESSFUL | new cartridge | setup (menu rebuilt) |
| `run`/`performance` succeeded | UPLOAD SUCCESSFUL | new cartridge | the cartridge |
| `debug` requested on hardware | none; refused before erase | unchanged | unchanged |
| CRC/length mismatch, invalid image, flash write error | red failure text | erased | setup |
| no frame for 10 s during an upload | UPLOAD TIMED OUT | erased | setup |
| verified but `run` could not start it | RUN FAILED | kept | setup |
| `RUN` succeeded | unchanged | unchanged | the cartridge |

Erasing a failed slot guarantees that setup never lists a partial image. A
`RUN` request is refused while an upload is in progress.

The table holds whichever setup screen is open. When an upload starts or a
cartridge is loaded, each setup screen that waits for input leaves
(`prg32_setup_interrupted()`) and control returns to the top-level menu, which
waits for the upload and then either hands over to the cartridge or rebuilds
its list of slots. "Setup" in the table is therefore always that top-level menu,
with its cursor kept. The same applies to
[`/api/games/select`](api.md). Settings already took effect when they were
changed, but text being typed and a network not yet confirmed are discarded.

Setup does not leave until the step in progress has finished: a physical button
still held, a Wi-Fi scan, a Cartridge Store request or download, the audio test
tune, or a timed message. Those steps can draw over the upload screen until
then. Two transfers are not coordinated, so do not start a USB upload during a
Store download.

Physical firmware rejects the protocol `debug` command because its
instruction-interpreter semantics are implemented by PRG32-Qt. Native
ESP32-C6 cartridges are debugged through the same connector's USB Serial/JTAG
hardware interface with OpenOCD, GDB, and the symbol ELF produced by
`cartridge build --debug-elf`; see the
[cartridge guide](cartridges.md#debug-a-cartridge-on-esp32-c6-over-usb).

## SDK examples

```bash
python3 -m prg32 esp32c6 upload game.prg32 \
  --port /dev/cu.usbmodem101 --slot cart0 --command run
python3 -m prg32 esp32c6 upload game.prg32 \
  --port socket://127.0.0.1:5555 --command performance
python3 -m prg32 esp32c6 upload game.prg32 \
  --port socket://127.0.0.1:8081 --command debug
python3 -m prg32 esp32c6 performance \
  --port socket://127.0.0.1:8081 --out performance.json
```

Key commands use `key` on either target (`qemu key` defaults to
`socket://127.0.0.1:5555`). Each argument is one step, pressed after the
previous one is released, and `+` presses keys together:

```bash
python3 -m prg32 esp32c6 key UP UP A --port /dev/cu.usbmodem101
python3 -m prg32 esp32c6 key A+B --hold-ms 500 --port /dev/cu.usbmodem101
python3 -m prg32 esp32c6 key RIGHT --hold-ms 5000 --port /dev/cu.usbmodem101
python3 -m prg32 esp32c6 key --release --port /dev/cu.usbmodem101
python3 -m prg32 qemu key START
```

The SDK prints the device's firmware and target, refuses `debug` before
contacting the slot when `debug_supported` is false, limits `--chunk-size` to
the device's `max_chunk`, prints progress every 10%, and resends a chunk once
if its reply is lost.

Only initial firmware installation uses the ROM flasher. Subsequent cartridge
development uses the running-device protocol and does not require network
connectivity.

## Validation record

### 2026-10-30: a connected console lists networks

Opening INFRASTRUCTURE while connected used to scan for about 600 ms and show NO
NETWORKS FOUND, every time. Stopping the radio raised a disconnect event whose
handler waited 1 s and then called `esp_wifi_connect()`, which ended the scan.
`scan_networks_locked()` now clears `sta_autoconnect` before the stop, and the
handler checks it again after the wait. While the radio is stopped the status is
OFF and the IP is `-`, because that event no longer changes them. `WIFI_SCAN` from a
connected station did not stop the radio and is unchanged. The wire format did not
change.

On ESP32-C6 (`prototype1`), with the serial log counting what each scan found:

- Before: 0 networks from a connected console in each of three scans, 24 when
  already disconnected. After: 22 to 25 in all four scans.
- With the fix, these rows of the
  [table below](#2026-10-30-paths-the-earlier-records-left-out) passed again: the
  list, keyboard, and confirmation exits (7 of the 8, without a `run` upload from the
  keyboard), saved access-point mode, `WIFI_SET` of an absent and a stored network,
  B from the no-networks and enterprise screens, and the connection wait. The rows
  that used the test build used it again, with the fix applied. `RUN` from the
  no-networks and enterprise screens was not repeated.
- Time from B on the network list until the HTTP API answered, six runs each:
  5.0 to 29.0 s before (median 7), 6.4 to 35.1 s after (median 13). The poll can
  take up to 11 s, so these are coarse; the runs do not show whether the fix
  changes this time.

`tests/test_wifi_protocol.py` fails if either check is removed. QEMU has no Wi-Fi;
its firmware was only built. No physical button was pressed.

The tests wrote `cart7` and NVS, both restored from `esptool read_flash`, and compared
with the backup.

### 2026-10-30: paths the earlier records left out

Firmware `c14d1dd` on ESP32-C6 (`prototype1`) and, built from the same source,
on Espressif QEMU. Key commands drove the screens; no physical button was pressed
and the panel was not looked at, so the screen evidence is `/api/screenshot.bmp`.
The wire format did not change.

| Check | Result |
|---|---|
| HELLO, `store`, `run`, bad slot, corrupt frame, wrong image CRC, `RUN` during an upload, abandoned upload, reconnection, unknown key | 11/11 on both targets |
| [Key gameplay check](../../tests/key_gameplay/README.md) | 29/29 on both targets |
| Setup takeover: every screen, by `run` upload and by `RUN` | 21/21 on QEMU; on the ESP32-C6 all passed once the check allowed for a screenshot caught mid-draw |
| `/api/games/select` from the top menu, ABOUT, RUN CARTRIDGE, SETUP, and the Store keyboard | 5/5, ESP32-C6 |
| Network list, keyboard, and confirmation with networks really listed: B and `RUN` from each, `run` upload from the first two | 8/8, ESP32-C6 |
| Saved access-point mode: set up with keys, B and `RUN` from the list | radio back in softAP, seen in the serial log |
| `WIFI_SET` during the list: absent network | refused with `NO AP FOUND`; B restored the saved network |
| `WIFI_SET` during the list: stored network | joined; leaving did not restart the radio |
| No-networks screen: B and `RUN` | passed |
| Enterprise-network screen: B and `RUN` | passed |
| Connection wait: connected, left by `RUN` after 329 ms, left by B after 586 ms, wrong password | passed; the wrong password ended at 30 s |
| Store found-URL screen: B, SELECT saves, `run` upload | passed |
| Store installed screen: B, SELECT runs it, `RUN`, `run` upload | passed |

The no-networks, enterprise, and connection-wait rows, and the `WIFI_SET` of a stored
network, ran on a scratch test build of the same source. It logs screen titles to the serial port, forces
the no-networks and enterprise cases, lists only stored networks, and reuses the
stored password on request. The Store rows used a local CartridgeStore and a small
mDNS responder sending PTR, SRV, TXT, and A records, as zeroconf does; the Store's own
zeroconf advertisement was not run.

Problems found, none caused by this revision's changes. The first is
[fixed](#2026-10-30-a-connected-console-lists-networks); the others are still open:

- **A connected console listed no networks.**
- **SELECT on Store details also presses RUN NOW over key commands.** `run_detail()`
  starts the download without waiting for the release, so the key command is
  still down afterwards and the installed screen is skipped. A works. A button held
  until the download ends should do the same; this was not tried.
- **Discovery silently misses an advertisement without its address.** A service
  registered with macOS `dns-sd`, seen by other computers, gave NOT FOUND in under
  2 s with no log line.
- **Setup screens can keep a cartridge's chrome.** After DeviceDemo+ ran, the Store
  screens still showed its title and FPS text.

The two records below say the network list, keyboard, and confirmation were
exercised. Those runs opened them from a connected console, so what they exercised
was the no-networks screen; this record replaces them for those screens.

Still not exercised: physical buttons and the panel, the real Store's mDNS
advertisement, and a real enterprise network.

The tests wrote `cart7` and NVS, both restored from `esptool read_flash`. All eight
slots, NVS, and `scores` were compared with the backup and are identical.

### 2026-10-30: saved Wi-Fi mode returns after a setup scan

Leaving the setup network screens without connecting now
[starts the saved Wi-Fi mode again](../usage/network.md#setup-menu-configuration).
Before, the scan left the radio stopped until reboot, so the HTTP API stopped
answering. The wire format did not change.

Each case ran on firmware for ESP32-C6 (`prototype1`) on `/dev/cu.usbmodem1101`,
in infrastructure mode and connected, after a restart. Key commands opened the
network list, and `/api/runtime` and `/api/screenshot.bmp` were polled while it
was open and after leaving.

- Control, on firmware built before this change: B from the list and `RUN` from
  the list both left HTTP down for the full 30 s observed.
- B from the network list, keyboard, and confirmation screen: both endpoints
  answered 4.9 to 6.0 s later, and the screenshot showed the Wi-Fi menu with
  status CONNECTED.
- `RUN` from the same three screens: the cartridge started, and both endpoints
  answered 7.4 to 8.0 s later with the cartridge on screen.
- HTTP was down on every case while the list was open, so each one left a
  stopped radio. The confirmation screen cannot be seen with the radio off; the
  key sequence that reaches it was checked by screenshot on the AP keyboard, where
  it moved on to the next field.

Not exercised: a saved access-point mode, the no-networks screen, a `run` upload,
and a `WIFI_SET` arriving while the list is open. The board's saved mode is
station. No network was confirmed, so these screens saved no setting.

No cartridge slot was written; all eight were read with `esptool read_flash`
before and after the tests and are identical.

### 2026-10-30: setup screens give way to a cartridge

Every setup screen that waits for input now leaves when an upload starts or a
cartridge is loaded ([device behavior](#device-behavior)). Each screen was opened
with key commands on firmware for ESP32-C6 (`prototype1`) on
`/dev/cu.usbmodem1101` and for Espressif QEMU, then a cartridge was started with
a `run` upload and, separately, with `RUN`. The check read the state the
cartridge drew and pressed START and RIGHT to prove it received the keys:
`/api/screenshot.bmp` on the ESP32-C6, the panel framebuffer on QEMU.

- Both targets: `cartridge_picker`, `cartridge_context_menu`,
  `default_cartridge_picker`, `settings_menu`, `audio_menu`, `developer_menu`,
  `about_menu`, the Store setup menu, and the keyboard opened from it. QEMU passed
  21/21.
- ESP32-C6 only, because QEMU has no Wi-Fi or Store connection: the Wi-Fi mode
  menu, forget list and confirmation, AP keyboard, Store list, and Store details.
  With the screens above, the board passed 33/33.
- A `store` upload over two submenus and a corrupt image over a third ended on the
  top-level menu with keys working, on both targets.
- The Wi-Fi network list, password keyboard, and confirmation, on the ESP32-C6: a
  setup scan leaves the radio stopped, so the screenshot is unavailable. All six
  cases replied successfully and the serial log showed the cartridge starting and
  no later setup lines; pressing keys and reading its screen was not possible.
- The commands `esp32c6 key A` and `esp32c6 upload ... --command run` showed the
  cartridge in place of RUN CARTRIDGE on both targets.
- A cartridge's own `prg32_text_input` still waits for its player on both targets:
  only an open setup menu interrupts it.
- Control: firmware built from the previous commit stayed in RUN CARTRIDGE for both
  a `run` upload and `RUN` on QEMU, and in ABOUT for a `run` upload.

Not exercised: the Wi-Fi connection wait, the no-networks and enterprise-network
screens, the Store's found-URL and installed screens, and `/api/games/select`.
Those paths use the same check, and `tests/test_setup_takeover.py` fails when a
listed screen lacks it. A cartridge blocked inside its update, as in
`prg32_text_input`, holds the framebuffer, so `/api/screenshot.bmp` returned no
image on the ESP32-C6 until it continued.

The tests wrote only `cart7`, which was backed up with `esptool read_flash` and
restored; all eight slots were compared with the backup and are identical. NVS
listed the same 87 entries afterwards.

### 2026-10-30: key commands

The KEY message, the `keys` HELLO capability and field, and the `esp32c6 key`
and `qemu key` commands were added. Firmware built from this revision for
ESP32-C6 (`prototype1`) on `/dev/cu.usbmodem1101` and for Espressif QEMU on
`socket://127.0.0.1:5555` passed the same 30 checks on both targets: HELLO,
`store`, `run`, press and automatic release, long hold, release of named and of
all keys, the defaults, `SELECT`, every refusal, release on `RUN` and on an upload,
refusal during an upload, a corrupt frame, an abandoned upload, reconnection,
and the A+B+DOWN restart.

Effect on the running program, compared on the screen:

- Input Tester, on the ESP32-C6 through `/api/screenshot.bmp` and on QEMU through
  the panel framebuffer: an idle screen changed no pixels, each of the seven
  keys changed the same 523 pixels and the screen returned to idle on release,
  and A+B changed 1,095 and showed `Multiple keys registered`. The screenshot
  reports the frame the firmware drew; the panel itself was not photographed.
- The [key gameplay check](../../tests/key_gameplay/README.md), a deterministic
  game played only with key commands, passed 29/29 on both targets, at about 127
  ms per frame on the ESP32-C6 and 33 ms on QEMU. Every press was exactly one move,
  including 20 presses of 1 ms, and a 2 s hold covered 21 frames on the ESP32-C6.
- Setup menu: DOWN DOWN moved the cursor and UP UP restored the same screen
  exactly on QEMU; on the ESP32-C6 A entered a submenu and B returned, seen in
  the serial log, and the cursor moved in the screenshot.
- Cartridges: see
  [Store key check](../cartridge_store/cartridge_store.md#store-key-check-2026-10-30).

Known limit at this revision, older than this message and
[fixed the same day](#2026-10-30-setup-screens-give-way-to-a-cartridge): the
setup menu left for a cartridge only from its top level. A `run` upload finished
while `cartridge_picker` (RUN CARTRIDGE) was open loaded the cartridge but left
that menu on screen, and keys then went to the menu.

All 32 CartridgeStore cartridges were run on both targets with the same check.
The tests wrote only `cart7`, which was backed up with `esptool read_flash` and
restored afterwards; `cart0` to `cart6` were compared and are unchanged. In NVS
only Wi-Fi driver and PHY calibration entries changed.

### 2026-10-30: Wi-Fi messages

WIFI_SCAN, WIFI_SET, and WIFI_FORGET were added, and HELLO advertises the
`wifi` capability. Exercised on the ESP32-C6 prototype on
`/dev/cu.usbmodem101` with `python3 -m prg32 esp32c6 wifi-scan`, `wifi-set`,
and `wifi-forget`:

- scan while connected listed the visible networks with the stored one marked,
  without dropping the connection;
- joining a network that does not exist replied `NO AP FOUND` and the board
  returned to its previous network;
- forgetting an unknown SSID was refused; forgetting the network in use
  switched the board to access-point mode and left no stored network;
- setting the network again joined it, and after a reset the board rejoined it
  on the first attempt;
- uploads, a corrupted payload, a bad frame, and `run` behaved as before.

On QEMU HELLO omits `wifi` and the three messages reply
`Wi-Fi is not available in this build`. The four-network limit and switching
to another stored network at boot were not exercised on hardware: only one
joinable network was available.

### 2026-10-29: heap reduction, payload validation, and whole frames

The receiver now runs the full cartridge validation, including the payload
CRC stored in the header, on the flash-mapped image at UPLOAD_END; HELLO
reports `heap_free` and `heap_largest`; the ESP32-C6 transmit ring shrank to
1,024 bytes; and reply frames are written whole, with the console routed
through the protocol driver. Before this change a completed PerformanceTest
result could not be read on the ESP32-C6: console log lines, written straight
to the USB Serial/JTAG FIFO, were inserted inside the 5,133-byte RESULT_DATA
frame and broke its CRC. The prototype's setup menu logs an RMT flush timeout
about every 140 ms, which made this frequent.

Firmware built from this revision for ESP32-C6 (`breadboard1`) and QEMU was
exercised with HELLO, `store`, `run`, and `performance` uploads, an image
with a correct transport CRC but a corrupted payload, an oversize image, a
frame with a bad CRC, repeated `RUN` switches, and reconnection.

- ESP32-C6 prototype on `/dev/cu.usbmodem101` and Espressif QEMU on
  `socket://127.0.0.1:5555`: the corrupted payload was rejected with
  `cartridge payload CRC mismatch` and the slot was left erased; the oversize
  image was refused before erasing; the bad frame did not disturb the
  following HELLO; every other request succeeded.
- The completed PerformanceTest result (5,129 bytes on ESP32-C6, 5,062 bytes on
  QEMU) was read intact six times in a row on the board while the setup menu
  kept logging, and console logs still reached the USB port.
- All 32 cartridges published on the CartridgeStore were run on both targets;
  see [Store compatibility check](../cartridge_store/cartridge_store.md#store-compatibility-check-2026-10-29).
- The tests wrote `cart6` and `cart7`; both slots were backed up with
  `esptool read_flash` beforehand and restored afterwards.

### 2026-10-28: robustness and throughput revision

Firmware built from this revision for ESP32-C6 (`breadboard1`) and QEMU, then
exercised with an end-to-end script covering HELLO, a corrupt frame, debug
refusal, `store` and `run` uploads (including while a cartridge was running),
a duplicated chunk, an out-of-order chunk, an abandoned upload, reconnection,
and a PerformanceTest upload with result retrieval.

- ESP32-C6 prototype on `/dev/cu.usbmodem101`: 15/15 checks passed. The
  abandoned upload timed out after 10 s, erased `cart5` and returned to setup;
  opening and closing the port no longer restarted the firmware.
- Espressif QEMU on `socket://127.0.0.1:5555`: 15/15 checks passed, with
  screenshots of the progress screen, the refreshed setup menu after `store`,
  and the timeout returning to setup.
- Throughput for the 23,863-byte DeviceDemo on ESP32-C6: 6.7 s before the
  revision (1 KiB chunks), 1.7 s after (about 50 KiB/s, of which 1.2 s is the
  intentional result hold). UPLOAD_BEGIN fell from 3.6–8.8 s to 0.25 s because
  only the needed sectors are erased, and the host no longer waits for the
  0.2 s serial timeout on every reply.

PerformanceTest retrievals reported their valid pre-completion state
(`running`); no completed interactive benchmark result is claimed.

### 2026-10-27: initial protocol

DeviceDemo and PerformanceTest were uploaded on ESP32-C6, QEMU, and PRG32-Qt
on Apple Silicon macOS (four CTest targets and documentation checks passed in
PRG32-Qt). That run did not detect the connection-time reset described under
[Modem-control lines](#modem-control-lines).
