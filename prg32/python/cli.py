"""The `python3 -m prg32 python` commands: new, check, to-c, to-asm and build.

Every command reads game.py as text and compiles it; none of them imports or
runs the student's program. `build` writes the generated C (or assembly) next
to the cartridge, so students can always open the file that was compiled.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from prg32.utilities.logging import die, log_info, log_ok
from prg32.utilities.runtime_handler import parse_cart_ram_kib

from . import asm_generator, compiler, generator


def read_source(path: str) -> str:
    """Read game.py as UTF-8 text. The file is never imported or executed."""
    source_path = Path(path)
    if not source_path.is_file():
        die(f"Missing PRG32 Python source: {path}")
    try:
        return source_path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        die(f"{path} is not UTF-8 text")
    return ""


def project_from_args(args: argparse.Namespace, source_path: str) -> dict[str, Any]:
    """The project details the compiler reads: title, entry prefix and author.

    The title defaults to the file's folder name (examples/games/pong/python/game.py
    gives "pong"), the entry prefix to the title made into a C name.
    """
    path = Path(source_path).resolve()
    default_title = path.parent.parent.name if path.parent.name == "python" else path.stem
    title = getattr(args, "name", None) or getattr(args, "title", None) or default_title
    project: dict[str, Any] = {"title": title}
    if getattr(args, "entry_prefix", None):
        project["entry_prefix"] = args.entry_prefix
    if getattr(args, "author", None):
        project["author"] = args.author
    return project


def compile_ir(args: argparse.Namespace) -> dict[str, Any]:
    """game.py -> game IR, or stop with the problem and its line."""
    source = read_source(args.source)
    try:
        return compiler.python_to_ir(source, project_from_args(args, args.source))
    except compiler.PythonError as error:
        die(f"{args.source}:{error.line}:{error.column}: {error.message}")
    return {}


def write_text(text: str, out: str | None) -> None:
    """Write generated text to a file, or print it when no file is given."""
    if out is None:
        print(text, end="" if text.endswith("\n") else "\n")
        return
    out_path = Path(out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text if text.endswith("\n") else text + "\n", encoding="utf-8")
    log_ok(f"wrote {out_path}")


def python_new(args: argparse.Namespace) -> None:
    """Write the starter game.py."""
    out_path = Path(args.source)
    if out_path.exists() and not args.force:
        die(f"{out_path} already exists; pass --force to overwrite it")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(compiler.STARTER, encoding="utf-8")
    log_ok(f"wrote the PRG32 Python starter to {out_path}")


def python_check(args: argparse.Namespace) -> None:
    """Report the problems in game.py (none means it compiles)."""
    source = read_source(args.source)
    problems = compiler.check(source, project_from_args(args, args.source))
    if args.json:
        print(json.dumps(problems, indent=2, sort_keys=True))
    else:
        for problem in problems:
            print(f"{args.source}:{problem['line']}:{problem['column']}: "
                  f"error [{problem['code']}]: {problem['message']}")
    if problems:
        raise SystemExit(1)
    if not args.json:
        log_ok(f"{args.source} is valid {compiler.LANGUAGE}")


def python_c(args: argparse.Namespace) -> None:
    """Translate game.py into commented C."""
    game_ir = compile_ir(args)
    write_text(generator.ir_to_c(game_ir, explain=not args.no_explain), args.out)


def python_asm(args: argparse.Namespace) -> None:
    """Translate game.py into commented RISC-V assembly."""
    game_ir = compile_ir(args)
    write_text(asm_generator.ir_to_asm(game_ir), args.out)


def python_build(args: argparse.Namespace) -> None:
    """Translate game.py and build a portable PRG32 cartridge from the result."""
    # Imported here so that `new`, `check`, `c` and `asm` work without the cartridge tooling.
    from prg32.cartridge.build_cartridge import build_cartridge_core
    from prg32.utilities.runtime_handler import resolve_cart_ram_size

    game_ir = compile_ir(args)
    out_path = Path(args.out)
    if args.asm:
        generated = asm_generator.ir_to_asm(game_ir)
        source_path = out_path.with_suffix(".S")
    else:
        generated = generator.ir_to_c(game_ir)
        source_path = out_path.with_suffix(".c")
    write_text(generated, str(source_path))

    features = list(game_ir.get("required_features", []))
    for feature in args.required_feature:
        if feature not in features:
            features.append(feature)
    cart_ram_size, ram_source = resolve_cart_ram_size(args.cart_ram_kib)
    log_info(f"{compiler.LANGUAGE}: entry prefix {game_ir['entry_prefix']}, "
             f"required features {features or 'none'}")
    log_info(f"Cartridge RAM limit: {cart_ram_size} bytes ({ram_source})")
    build_cartridge_core(source=str(source_path),
                         out=str(out_path),
                         entry_prefix=game_ir["entry_prefix"],
                         tool_prefix=args.tool_prefix,
                         march=args.march,
                         mabi=args.mabi,
                         required_features=features,
                         multiplayer="multiplayer" in features,
                         audio_block=args.audio_block,
                         name=game_ir["title"],
                         cart_ram_size=cart_ram_size)


def add_python_commands(sub: argparse._SubParsersAction, subparsers_action: type) -> None:
    """Add the `python` command group to the main PRG32 parser."""
    python_p = sub.add_parser("python", help="PRG32 Python tasks (new, check, to-c, to-asm, build)")
    python_sub = python_p.add_subparsers(dest="sub_cmd", required=True, action=subparsers_action)

    def project_options(p: argparse.ArgumentParser) -> None:
        p.add_argument("--name", help="game title (default: the game's folder name)")
        p.add_argument("--entry-prefix", help="prefix of the init/update/draw functions (default: from the name)")
        p.add_argument("--author", help="author recorded in the game description")

    p = python_sub.add_parser("new", help="write the PRG32 Python starter game", usage="%(prog)s GAME_PY [--force]")
    p.add_argument("source", help="path of the new game.py")
    p.add_argument("--force", action="store_true", help="overwrite an existing file")
    p.set_defaults(func=python_new)

    p = python_sub.add_parser("check", help="check a PRG32 Python game and list its problems",
                              usage="%(prog)s GAME_PY [--json]")
    p.add_argument("source", help="PRG32 Python source (game.py)")
    p.add_argument("--json", action="store_true", help="print the problems as JSON")
    project_options(p)
    p.set_defaults(func=python_check)

    p = python_sub.add_parser("to-c", help="translate a PRG32 Python game into commented C",
                              usage="%(prog)s GAME_PY [--out GAME_C] [options]")
    p.add_argument("source", help="PRG32 Python source (game.py)")
    p.add_argument("--out", help="output .c file (default: print to the terminal)")
    p.add_argument("--no-explain", action="store_true", help="leave out the explanatory comments")
    project_options(p)
    p.set_defaults(func=python_c)

    p = python_sub.add_parser("to-asm", help="translate a PRG32 Python game into commented RISC-V assembly",
                              usage="%(prog)s GAME_PY [--out GAME_S] [options]")
    p.add_argument("source", help="PRG32 Python source (game.py)")
    p.add_argument("--out", help="output .S file (default: print to the terminal)")
    project_options(p)
    p.set_defaults(func=python_asm)

    p = python_sub.add_parser("build", help="build a .prg32 cartridge from a PRG32 Python game",
                              usage="%(prog)s GAME_PY --out OUT_PATH [options]")
    p.add_argument("source", help="PRG32 Python source (game.py)")
    p.add_argument("--out", required=True, help="output cartridge (.prg32); the generated .c or .S is written beside it")
    p.add_argument("--asm", action="store_true", help="build from the generated assembly instead of the generated C")
    project_options(p)
    p.add_argument("--architecture", choices=["esp32c6", "qemu"],
                   help="target architecture (informational: portable cartridges run on both)")
    p.add_argument("--audio-block", help="optional PRG32 AUDIO block produced by tools/prg32audio_pack.py")
    p.add_argument("--required-feature", action="append", default=[],
                   help="an extra required PRG32 feature (the compiler adds the ones the game uses)")
    p.add_argument("--march", default="rv32imc_zicsr_zifencei", help="RISC-V architecture string")
    p.add_argument("--mabi", default="ilp32", help="RISC-V ABI string")
    p.add_argument("--tool-prefix", default="riscv32-esp-elf-", help="prefix of the RISC-V GCC toolchain")
    p.add_argument("--cart-ram-kib", type=parse_cart_ram_kib, default=None,
                   help="executable cartridge RAM of the target firmware in KiB (default: 64)")
    p.set_defaults(func=python_build)
