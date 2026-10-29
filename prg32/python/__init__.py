"""PRG32 Python: write a PRG32 cartridge in a small, typed part of Python.

PRG32 Python is an authoring language, not a runtime. No Python interpreter runs
on the console, and the SDK never imports or runs game.py either:

    game.py
      -> ast.parse with size and nesting limits      (compiler.parse)
      -> allow-list and static type checks            (compiler.python_to_ir)
      -> the game IR shared with the Construction Kit (generator.IR_ABI)
      -> commented C (generator.ir_to_c) or RISC-V assembly (asm_generator.ir_to_asm)
      -> python3 -m prg32 cartridge build             (prg32.cartridge.build_cartridge)

The modules are shared with PRG32-Construction-Kit, so the same game.py gives the
same C and assembly in the kit's IDE and in the SDK. The language is described in
docs/software/python_language.md; the command line in cli.py.
"""

from __future__ import annotations

from .compiler import LANGUAGE, LANGUAGE_VERSION, STARTER, PythonError, check, parse, python_to_ir

__all__ = ["LANGUAGE", "LANGUAGE_VERSION", "STARTER", "PythonError", "check", "parse", "python_to_ir"]
