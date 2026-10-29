# PRG32 Python Consistency

PRG32 Python is implemented twice: in `prg32/python/` here and in
PRG32-Construction-Kit (`prg32_construction_kit/python_project.py`,
`generator.py`, `expressions.py`, `asm_generator.py`). The same `game.py` must
give byte-identical C and assembly in both, apart from the `GENERATED_BY` name.

- Treat the kit as the origin of the language. Change the language there first,
  then copy the modules here as described in
  [Keeping the SDK and the Kit Aligned](../software/python_language.md#keeping-the-sdk-and-the-kit-aligned).
- Mark every SDK-only line with an `SDK:` comment and keep such lines few.
- Never run, import, or `exec` a student's `game.py`; only `ast.parse` it.
- Run `tests/test_prg32_python.py` with a kit checkout beside this repository
  (or `PRG32_CONSTRUCTION_KIT`) so the parity test is not skipped, and with the
  RISC-V toolchain so cartridge builds are exercised.
- Update [PRG32 Python](../software/python_language.md) in the same change as
  any command, option, rule, or limit.
