# Assembly Example Guidelines

Example assembly is teaching material. It should be verbose and heavily
commented enough for first-year students.

Every example game directory should contain:

```text
examples/games/<name>/
|-- README.md
|-- ascii/game.S
|-- graphics/game.S
`-- c/game.c
```

When adding a game:

1. Add both `ascii/game.S` and `graphics/game.S` unless the user asks for only one.
2. Add a `c/game.c` version when the game is part of the standard teaching set.
3. Add a local `README.md` with controls and learning goals.
4. Update `examples/games/README.md`.
5. Update the example list in `README.md`.
6. Add `PRG32_GAME_<NAME>_ASCII` and `PRG32_GAME_<NAME>_GRAPHICS` identifiers in
   `main/prg32_config.h` if the examples list uses selection constants.
7. Add `PRG32_GAME_<NAME>_C` when a C version is provided.

Assembly conventions:

- Use `.option norelax` at the top of RISC-V example files.
- Save and restore `ra` around any call into C.
- Keep stack alignment at 16 bytes around C calls.
- Prefer `t0`-`t6` for temporary values that do not need to survive C calls.
- Use `s0`-`s11` only when saving/restoring them in the stack frame.
- Avoid relying on `t` or `a` registers surviving a `call`.
- Use named local labels such as `.Lasteroids_done` for larger routines.
- If numeric local labels are used, keep them simple and readable.
- Do not make the examples too feature-rich; each one should demonstrate a few
  concepts clearly: input bitmasking, memory variables, drawing, sound, and calls.

## Assembly Code Validation

For assembly-only example changes, also inspect:

```bash
rg -n "^[0-9]{2,}:|[0-9]{2}f|[0-9]{2}b" examples/games/<name>
```

This catches accidental multi-digit numeric labels in classroom assembly.
