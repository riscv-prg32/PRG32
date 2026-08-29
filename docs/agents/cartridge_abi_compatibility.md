# PRG32 Cartridge ABI Compatibility

- Treat `prg32/abi/prg32_abi.json` as the single source of truth for the portable
  cartridge ABI.
- Do not edit generated ABI files manually. Regenerate or check them with
  `python3 python3 -m prg32 abi gen` and `python3 python3 -m prg32 abi gen --check`.
- ABI function indices are append-only. Never reorder, remove, or change
  existing function prototypes within the same ABI major version.
- Any incompatible ABI change requires increasing `PRG32_ABI_MAJOR`.
- Additive functions or optional feature bits may increase `PRG32_ABI_MINOR`.
- Portable cartridges must not link against firmware-specific symbol addresses.
- Legacy absolute-import cartridges may remain supported, but new examples and
  documentation should prefer portable ABI-table cartridges.
- Upload, QEMU staging, CartridgeStore downloads, and firmware setup downloads
  should reject incompatible cartridges before deployment whenever the ABI
  contract can be checked.

For cartridge/ABI work, run the relevant available checks before finishing:

```bash
python3 python3 -m prg32 abi gen --check
python3 -m prg32 cartridge summary build/<example>.prg32
git diff --check
```

If `pytest`, ESP-IDF, or the RISC-V toolchain is unavailable, state exactly
which checks could not be run.
