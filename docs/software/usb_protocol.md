# PRG32 Device Protocol

`prg32-io-1.0` is the binary development protocol shared by ESP32-C6 native
USB, QEMU UART-over-TCP, and the loopback-only PRG32-Qt development listener.
It lets the SDK upload, run, debug where supported, and retrieve performance
results without Wi-Fi. The running firmware recognizes the first valid frame;
there is no upload mode and no reset into the ROM bootloader.

## Transport and framing

- ESP32-C6: USB Serial/JTAG CDC, normally 115200 baud.
- QEMU: UART 0 TCP server, commonly `socket://127.0.0.1:5555`.
- PRG32-Qt: `socket://127.0.0.1:8081`; the listener is deliberately loopback
  only because the protocol has no authentication.

Each frame begins with this packed, little-endian 24-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `PRG32IO\0` magic |
| 8 | 1 | version (`1`) |
| 9 | 1 | message type |
| 10 | 2 | flags |
| 12 | 4 | request ID |
| 16 | 4 | payload size, at most 16384 |
| 20 | 4 | IEEE CRC-32 of the payload |

The magic makes decoding self-synchronizing when firmware logs share the
stream. Every request receives a response with the same request ID. Status
responses use compact UTF-8 JSON with `ok` and either `message` or `error`.

## Messages

| Type | Name | Payload |
|---:|---|---|
| `0x01` | HELLO | empty/JSON; returns capabilities and runtime contract |
| `0x02` | UPLOAD_BEGIN | metadata JSON |
| `0x03` | UPLOAD_DATA | `uint32 offset` followed by up to 4096 bytes |
| `0x04` | UPLOAD_END | optional JSON; verifies size and CRC |
| `0x05` | RUN | JSON containing `slot` |
| `0x06` | DEBUG | JSON containing `slot`; PRG32-Qt only |
| `0x07` | PERFORMANCE_GET | empty; returns result data |
| `0x80` | STATUS | response JSON |
| `0x81` | RESULT_DATA | performance JSON; flag bit 0 marks the final chunk |

UPLOAD_BEGIN metadata contains `name`, `slot`, `size`, `crc32`, and `command`.
The command is `store`, `run`, `debug`, or `performance`. Firmware validates
metadata before erasing the target slot, requires ordered chunks, computes CRC
while streaming, and validates the cartridge before selecting it.

## Device behavior

Receiving UPLOAD_BEGIN automatically unloads the active cartridge and opens an
upload screen. It shows the cartridge name, target slot, byte percentage, and
progress bar. Completion is held long enough to show success or failure. A
`store` operation returns to setup; `run` and `performance` select the uploaded
cartridge. Physical firmware rejects `debug` explicitly because instruction
debugging is implemented by PRG32-Qt.

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

Only initial firmware installation uses the ROM flasher. Subsequent cartridge
development uses the running-device protocol and does not require network
connectivity.

## Validation record

On 2026-10-27 the protocol was exercised with DeviceDemo and PerformanceTest
on all three hosts:

- ESP32-C6 prototype on `/dev/cu.usbmodem1101`: firmware flash passed;
  automatic uploads to `cart1` and `cart2`, run selection, progress UI, and
  performance-status retrieval passed without re-entering the bootloader.
- Espressif QEMU: clean firmware build passed; live uploads through
  `socket://127.0.0.1:5555`, run selection, and performance-status retrieval
  passed.
- PRG32-Qt on Apple Silicon macOS: native application build, DeviceDemo upload
  and debug launch through `socket://127.0.0.1:8081`, performance retrieval,
  four CTest targets, documentation checks, and formatting checks passed.

The PerformanceTest retrievals reported their valid pre-completion states
(`running` or `no performance test results`); no completed interactive
benchmark result was claimed.
