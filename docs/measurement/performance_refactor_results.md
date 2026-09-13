# Performance Cartridge Refactor Validation

This document records the bounded validation evidence for the broker refactor.
It is not an operating manual. Use the
[Performance Test Guide](../performance_test.md) to reproduce a run, retrieve
the compact result, interpret its statistics, or develop a custom test
cartridge.

The baseline is commit `c838cd9`, built with the same ESP-IDF 5.4 QEMU
configuration as the refactored tree on 2026-09-06.

| QEMU linker/build measurement | Baseline | Refactored | Delta |
|---|---:|---:|---:|
| `.dram0.bss` | 169,080 B | 169,392 B | +312 B |
| `.dram0.data` | 5,804 B | 5,788 B | -16 B |
| `.iram0.text` | 120,724 B | 120,724 B | 0 B |
| firmware binary | 849,104 B | 808,992 B | -40,112 B |

The combined static DRAM change is +296 bytes relative to the old resident
implementation; the broker retains no raw frame array. One active reference case allocates exactly 240
bytes (60 32-bit frame times), then frees it at case end. The reference
cartridge executable uses 4,880 bytes of cartridge RAM and is unloaded normally
when another cartridge starts.

These are linker and allocation-design measurements, not live ESP32-C6 heap
measurements. Stable-idle free heap, largest block, peak/during-suite heap,
post-completion, post-abort, and ordinary-cartridge-afterward values must be
captured from a running target via the broker's `memory` JSON object before
publishing hardware RAM-recovery claims. No unmeasured “42 KiB recovered” claim
is made here.

## QEMU runtime result

A complete ESP32-C3 QEMU run of the store-ready reference cartridge was
executed on 2026-09-06. The broker reached `COMPLETE` with ten results,
`active_case == -1`, and its temporary sample pointer cleared.

| QEMU heap checkpoint | Free 8-bit heap | Largest block |
|---|---:|---:|
| Pre-suite baseline | 75,068 B | 57,344 B |
| Peak/during suite | 74,824 B | 57,344 B |
| Immediately after completion | 75,068 B | 57,344 B |

Peak temporary loss was 244 bytes and post-completion residual loss was zero
bytes. These QEMU figures validate successful completion and cleanup but must
not be presented as physical ESP32-C6 measurements. Post-abort and
ordinary-cartridge-afterward checkpoints still require a separate driven run.

The broker deliberately uses volatile RAM only and performs no flash writes, so
the refactor adds no flash-wear concern. Completed results survive cartridge
replacement but not reboot or a new suite.
