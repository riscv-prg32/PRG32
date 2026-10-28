# Device Protocol and Hardware Consistency

The `prg32-io-1.0` device protocol has four implementations that must stay
compatible, and the physical pin assignment appears in five places. Change
them together.

## Protocol implementations

| Implementation | Location |
|---|---|
| Firmware receiver | `components/prg32/prg32_usb_protocol.c` |
| Python SDK client | `prg32/usb_protocol.py`, `prg32/esp32c6/upload_esp32c6.py` |
| PRG32-Qt listener | `src/qt/IoProtocolServer.cpp` in the PRG32-QT repository |
| Browser client | `static/js/prg32_usb_protocol.js` in PRG32-Construction-Kit; CartridgeStore follows [web_usb_deployment.md](../software/web_usb_deployment.md) |

Rules:

- [usb_protocol.md](../software/usb_protocol.md) is the single wire-format
  reference. Update it in the same change as any framing, message, flag, field,
  timeout, or device-behavior change, and add a dated validation record.
- Changes inside `prg32-io-1.0` must be additive: new HELLO fields, new
  optional flags, and new messages. Hosts must keep working when a field is
  missing. An incompatible change needs a new protocol string and version byte.
- Validate a request completely before erasing a slot. Every failure after the
  erase must leave the slot erased, never partially written.
- Never change DTR and RTS one at a time in a host. See
  [modem-control lines](../software/usb_protocol.md#modem-control-lines).
- Run `tests/test_usb_protocol.py` (part of `python3 -m prg32 test`). When the
  firmware receiver changes and hardware is available, exercise HELLO, `store`,
  `run`, a corrupt frame, an abandoned upload, and reconnection on QEMU
  (`socket://127.0.0.1:5555`) and on ESP32-C6, and record the result.
- Companion repositories are not edited from this repository. Record
  required follow-ups in [web_usb_deployment.md](../software/web_usb_deployment.md#status-reviewed-2026-10-28)
  and tell the user.

## Pin assignment

The canonical source is `profiles/sdkconfig.pins.prototype1`. When a pin
changes, update in the same change:

1. [hardware.md](../hardware/hardware.md) wiring tables (follow the
   [hardware guidelines](hardware_guidelines.md));
2. `docs/hardware/pcb/PRG32-PCB-0-1.fzz`: the schematic wire *and* its
   connection records, and the PCB copper, or the
   [layout status](../hardware/pcb/README.md#layout-status) table when copper
   cannot be completed;
3. `docs/hardware/images/prg32-electrical-schematic.svg`, re-exported with
   `/Applications/Fritzing.app/Contents/MacOS/Fritzing -svg <folder>`;
4. `docs/hardware/images/prg32-breadboard-prototype.svg`, whose source sketch
   is not in the repository, by editing the affected wire endpoints;
5. the [bill of materials](../hardware/where_to_buy.md) review note.

A Fritzing wire records its endpoints in `<connect>` elements independently of
its drawn geometry. Check both: drawn lines can land on the right pin while
the recorded connection names another one.
