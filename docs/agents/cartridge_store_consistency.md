# CartridgeStore Consistency

PRG32 firmware and tools are paired with the CartridgeStore server.
Keep both repositories consistent.

Companion repository: https://github.com/riscv-prg32/CartridgeStore

## Canonical constants -- must match in both repos

| Constant | Value |
|---|---|
| mDNS service type | `_prg32store._tcp` |
| mDNS default port | `5080` |
| Discovery ABI | `prg32-store-discovery-1.0` |
| Metadata ABI | `prg32-metadata-1.0` |
| Colophon ABI | `prg32-colophon-1.0` |
| Architecture -- physical | `esp32c6` |
| Architecture -- QEMU | `qemu` |

## Rules

- Before changing any constant above in PRG32, verify it has the same value
  in CartridgeStore. If they differ, resolve the discrepancy before merging.
- Do not rename an architecture string without a coordinated change in
  CartridgeStore; the string is stored in the CartridgeStore SQLite database
  and in every firmware image that has that string baked via Kconfig.
- When adding a new CartridgeStore API call in PRG32 firmware or tools,
  cross-check `docs/api.md` in CartridgeStore for the exact request and
  response shape.
- The CartridgeStore `zeroconf` mDNS service name is the canonical source;
  `PRG32_STORE_MDNS_SERVICE` in `main/prg32_config.h` must mirror it.

## Integration files in this repo

| Path | Purpose |
|---|---|
| `components/prg32/prg32_store.c` | mDNS discovery, NVS URL, HTTP catalog client |
| `components/prg32/prg32_setup_store.c` | Setup menu screens: config + browser |
| `components/prg32/include/prg32.h` | `prg32_store_*` public declarations |
| `main/prg32_config.h` | `PRG32_STORE_*` compile-time constants |
| `python3 -m prg32` | `publish`, `pack-bundle`, `publish-bundle`, `store-*` |
| `docs/cartridge_store.md` | End-user integration guide |
