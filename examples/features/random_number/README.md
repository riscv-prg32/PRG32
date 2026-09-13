# Random Number Demo

The C and RISC-V assembly versions call `prg32_random_number(1, 90)` and
display the result. Press A to draw another number; holding A does not repeat.
Compare the ABI arguments in `a0` and `a1` with the C call, then inspect the
returned value in `a0`. The API contract is documented in
[the ABI guide](../../../docs/software/abi.md).

Build either version as a portable cartridge after sourcing ESP-IDF:

```bash
python3 -m prg32 cartridge build examples/features/random_number/demo.S \
  --out build/random_number_asm.prg32 --name RandomNumberAsm \
  --entry-prefix random_number --portable
python3 -m prg32 cartridge build examples/features/random_number/c/demo.c \
  --out build/random_number_c.prg32 --name RandomNumberC \
  --entry-prefix random_number_c --portable
```

Checkpoint: each distinct A press updates the displayed value to a number
from 1 through 90. Why does the assembly version save `ra` before calling a
framework function?
