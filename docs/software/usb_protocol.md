# PRG32 Device Protocol

`prg32-io-1.0` is the binary development protocol shared by ESP32-C6 native
USB, QEMU UART-over-TCP, and the loopback-only PRG32-Qt development listener.
It lets the SDK, the PRG32-Construction-Kit and the CartridgeStore upload, run,
debug where supported, and retrieve performance results without Wi-Fi. The
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
| `0x80` | STATUS | response JSON |
| `0x81` | RESULT_DATA | performance JSON; flag bit 0 marks the final chunk |

### HELLO response

```json
{"ok":true,"protocol":"prg32-io-1.0",
 "capabilities":["upload","run","performance"],"debug_supported":false,
 "firmware":"5202077","target":"esp32c6","slots":8,
 "max_chunk":4096,"max_payload":16384,
 "runtime":{"cart_ram_size":65536,"cart_max_size":65536,
            "cart_abi_major":1,"cart_abi_minor":1}}
```

`firmware`, `target`, `slots`, `max_chunk`, `max_payload` and
`runtime.cart_abi_minor` were added in October 2026. Hosts must treat missing
fields as unknown and fall back to 1024-byte chunks. `target` is the IDF target
(`esp32c3` for the QEMU build).

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
4. on UPLOAD_END checks length and CRC, validates the stored cartridge, and
   for `run`/`performance` selects it.

Every STATUS reply to UPLOAD_DATA reports `received` and `total` bytes.
UPLOAD_END returns `name`, `slot`, `size`, and `running`.

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

Erasing a failed slot guarantees that setup never lists a partial image. A
`RUN` request is refused while an upload is in progress.

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

The SDK prints the device's firmware and target, refuses `debug` before
contacting the slot when `debug_supported` is false, limits `--chunk-size` to
the device's `max_chunk`, prints progress every 10%, and resends a chunk once
if its reply is lost.

Only initial firmware installation uses the ROM flasher. Subsequent cartridge
development uses the running-device protocol and does not require network
connectivity.

## Validation record

### 2026-10-28: robustness and throughput revision

Firmware built from this revision for ESP32-C6 (`prototype1`) and QEMU, then
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
