# PRG32 Cartridge ABI Compatibility

- Treat `prg32/abi/prg32_abi.json` as the single source of truth for the portable
  cartridge ABI.
- Do not edit generated ABI files manually. Regenerate or check them with
  `python3 -m prg32 abi gen` and `python3 -m prg32 abi check`.
- ABI function indices are append-only. Never reorder, remove, or change
  existing function prototypes within the same ABI major version.
- Every JSON entry records the function's real return and argument types, as
  declared in the public headers, and a one-sentence `notes` description.
  `tests/test_prg32_abi_gen.py` checks the types; the generator rejects missing
  notes. Correcting recorded types changes the hash: list the previous hash in
  `compatible_hashes` when the table entries themselves are unchanged.
- Any incompatible ABI change requires increasing `PRG32_ABI_MAJOR`.
- Additive functions or optional feature bits may increase `PRG32_ABI_MINOR`.
- Portable cartridges must not link against firmware-specific symbol addresses.
- Legacy cartridges are linked to a fixed `prg32_cart_exec` address
  (0x408023a0 in the default ESP32-C6 `breadboard1` build). Code placed in IRAM
  before the window moves it, so after changing IRAM-placement options or
  IRAM code, check `riscv32-esp-elf-nm build-esp32c6/PRG32.elf | grep
  prg32_cart_exec` and keep the address unless the change is announced.
- Legacy absolute-import cartridges may remain supported, but new examples and
  documentation should prefer portable ABI-table cartridges.
- Upload, QEMU staging, CartridgeStore downloads, and firmware setup downloads
  should reject incompatible cartridges before deployment whenever the ABI
  contract can be checked.

For cartridge/ABI work, run the relevant available checks before finishing:

```bash
python3 -m prg32 abi check
python3 -m prg32 cartridge summary build/<example>.prg32
git diff --check
```

If `pytest`, ESP-IDF, or the RISC-V toolchain is unavailable, state exactly
which checks could not be run.
