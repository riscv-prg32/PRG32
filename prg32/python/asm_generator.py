"""Game IR -> RISC-V assembly (RV32IM) for the PRG32 portable cartridge ABI.

The assembly is a second view of the same game. It is generated from the same
validated game IR as the C file (``generator.ir_to_c``), block by block, so a
student can put the two files side by side:

* above the instructions of every block, a comment repeats the block in words
  and shows the C line that the instructions implement;
* every instruction has a comment that says what it does in this program;
* the file header explains the registers, the stack frame and every
  instruction and directive the file uses.

The translation is the scheme of a textbook one-pass compiler, chosen because
it is easy to follow, not because it is short:

* every expression leaves its value in register a0 (the "accumulator");
* a simple operand (a number, a constant or a variable) is loaded straight
  into t0, so ``x + 4`` becomes "load x, load 4, add";
* the left side of a longer operation waits in a numbered stack slot while
  the right side is computed, then comes back into t0;
* ``&&`` and ``||`` jump over the right side when the left side already
  decides the answer, exactly like C.

The kit helpers (lists, sine table, lines, circles, number printing, ...) are
hand-written in assembly with the same names, inputs and results as the C
helpers. Division needs no helper: the RISC-V DIV and REM instructions already
give the answers that the C helpers prg32_kit_div and prg32_kit_mod imitate.

The output is a ``.S`` file: the PRG32 builder runs it through the C
preprocessor, so full-line comments use ``/* ... */`` (a line starting with
``#`` would be read as a preprocessor directive) and ``#`` starts a comment
only after an instruction.
"""

# Shared with PRG32-Construction-Kit: prg32_construction_kit/asm_generator.py at revision 0b9f1c0.
# Keep the two copies identical apart from the lines marked "SDK:"; see
# docs/software/python_language.md#keeping-the-sdk-and-the-kit-aligned.

from __future__ import annotations

import textwrap
from typing import Any

from .expressions import (
    CONSTANTS,
    FUNCTIONS,
    ExpressionError,
    c_identifier,
    check_expr,
    parse_expr,
    render,
    state_name,
)
from .generator import (
    GENERATED_BY,
    BUTTON_MAP,
    COLOR_MAP,
    MAP_BEGIN,
    MAP_END,
    MAX_REPEAT,
    SINE_TABLE,
    _Emitter,
    _statements_read_input,
    bitmap_picture,
    blocks_to_ir,
    c_comment,
    c_int,
    color_name,
    describe_statement,
    is_python,
    narrate,
    int32,
    rgb565_words,
)

ASM_COMMENT_COLUMN = 44
_WRAP = 100

# A variable may not be called like a register: "la t0, a0" would not assemble.
REGISTER_NAMES = (
    {"zero", "ra", "sp", "gp", "tp", "fp", "pc"}
    | {f"x{i}" for i in range(32)} | {f"f{i}" for i in range(32)}
    | {f"t{i}" for i in range(7)} | {f"s{i}" for i in range(12)} | {f"a{i}" for i in range(8)}
    | {f"ft{i}" for i in range(12)} | {f"fs{i}" for i in range(12)} | {f"fa{i}" for i in range(8)}
)

# Firmware functions: the type of every parameter that is narrower than 32 bits.
# The RISC-V calling convention says the CALLER widens such a value to 32 bits
# (zero-extension for unsigned types, sign-extension for signed ones), so the
# generated code does what a C compiler does when it converts int -> uint8_t.
_NARROW = {"u8": "uint8_t", "u16": "uint16_t", "i16": "int16_t"}

# The instructions and pseudo-instructions this generator can write, explained.
ASM_GLOSSARY = {
    "add": "add rd, rs1, rs2: rd = rs1 + rs2 (32-bit, wraps around on overflow)",
    "addi": "addi rd, rs, imm: rd = rs + imm, imm is a constant from -2048 to 2047",
    "and": "and rd, rs1, rs2: bitwise AND, a bit is 1 only where both bits are 1",
    "andi": "andi rd, rs, imm: bitwise AND with a constant (keeps the bits set in imm)",
    "beq": "beq rs1, rs2, label: jump to label when rs1 == rs2",
    "beqz": "beqz rs, label: jump to label when rs == 0 (pseudo: beq rs, zero, label)",
    "bge": "bge rs1, rs2, label: jump when rs1 >= rs2 (signed comparison)",
    "bgeu": "bgeu rs1, rs2, label: jump when rs1 >= rs2 comparing as unsigned numbers",
    "bgez": "bgez rs, label: jump when rs >= 0 (pseudo: bge rs, zero, label)",
    "bgt": "bgt rs1, rs2, label: jump when rs1 > rs2 (pseudo: blt rs2, rs1, label)",
    "bgtu": "bgtu rs1, rs2, label: jump when rs1 > rs2 as unsigned numbers (pseudo: bltu rs2, rs1)",
    "ble": "ble rs1, rs2, label: jump when rs1 <= rs2 (pseudo: bge rs2, rs1, label)",
    "blez": "blez rs, label: jump when rs <= 0 (pseudo: bge zero, rs, label)",
    "bltz": "bltz rs, label: jump when rs < 0 (pseudo: blt rs, zero, label)",
    "blt": "blt rs1, rs2, label: jump when rs1 < rs2 (signed comparison)",
    "bne": "bne rs1, rs2, label: jump to label when rs1 != rs2",
    "bnez": "bnez rs, label: jump to label when rs != 0 (pseudo: bne rs, zero, label)",
    "call": "call f: jump to function f and put the return address in ra (pseudo: auipc + jalr)",
    "div": "div rd, rs1, rs2: signed division rounded toward zero; x / 0 = -1, never a trap",
    "divu": "divu rd, rs1, rs2: unsigned division",
    "j": "j label: jump to label (pseudo: jal zero, label)",
    "la": "la rd, symbol: rd = address of symbol, computed from the pc (pseudo: auipc + addi)",
    "lbu": "lbu rd, off(rs): load one byte from address rs + off, zero-extended to 32 bits",
    "lh": "lh rd, off(rs): load a 16-bit halfword, sign-extended to 32 bits",
    "lhu": "lhu rd, off(rs): load a 16-bit halfword, zero-extended to 32 bits",
    "li": "li rd, imm: rd = a constant of any size (pseudo: addi, or lui + addi)",
    "lw": "lw rd, off(rs): load the 32-bit word stored at address rs + off",
    "mul": "mul rd, rs1, rs2: rd = the low 32 bits of rs1 * rs2",
    "mv": "mv rd, rs: copy rs into rd (pseudo: addi rd, rs, 0)",
    "neg": "neg rd, rs: rd = -rs (pseudo: sub rd, zero, rs)",
    "not": "not rd, rs: flip every bit (pseudo: xori rd, rs, -1)",
    "or": "or rd, rs1, rs2: bitwise OR, a bit is 1 where either bit is 1",
    "rem": "rem rd, rs1, rs2: signed remainder, sign of rs1; x % 0 = x, never a trap",
    "remu": "remu rd, rs1, rs2: unsigned remainder",
    "ret": "ret: return to the caller (pseudo: jalr zero, 0(ra))",
    "sb": "sb rs, off(rb): store the low byte of rs at address rb + off",
    "seqz": "seqz rd, rs: rd = 1 if rs == 0, else 0 (pseudo: sltiu rd, rs, 1)",
    "sgtz": "sgtz rd, rs: rd = 1 if rs > 0, else 0 (pseudo: slt rd, zero, rs)",
    "sll": "sll rd, rs1, rs2: shift left by rs2 (only its low 5 bits count)",
    "slli": "slli rd, rs, n: shift left by n bits, the same as multiplying by 2^n",
    "slt": "slt rd, rs1, rs2: rd = 1 if rs1 < rs2 (signed), else 0",
    "sltz": "sltz rd, rs: rd = 1 if rs < 0, else 0 (pseudo: slt rd, rs, zero)",
    "snez": "snez rd, rs: rd = 1 if rs != 0, else 0 (pseudo: sltu rd, zero, rs)",
    "sra": "sra rd, rs1, rs2: arithmetic shift right: the sign bit is copied in",
    "srai": "srai rd, rs, n: arithmetic shift right by n bits (keeps the sign)",
    "srl": "srl rd, rs1, rs2: logical shift right: zeros come in from the left",
    "srli": "srli rd, rs, n: logical shift right by n bits (zeros come in)",
    "sub": "sub rd, rs1, rs2: rd = rs1 - rs2",
    "sw": "sw rs, off(rb): store the 32-bit word rs at address rb + off",
    "tail": "tail f: jump to f without saving a return address, so f returns straight to our caller",
    "xor": "xor rd, rs1, rs2: bitwise exclusive OR, a bit is 1 where the bits differ",
    "xori": "xori rd, rs, imm: exclusive OR with a constant (xori rd, rs, 1 flips 0 <-> 1)",
}

DIRECTIVES = [
    ".option norelax  keep every address pc-relative; the linker must not shorten it",
    ".equ NAME, value  give a name to a number (like #define in C)",
    ".section NAME     where the following bytes go: .text code, .rodata read-only data,",
    "                  .data variables with a starting value, .bss variables that start at 0",
    ".balign n         skip bytes until the address is a multiple of n",
    ".word / .half / .byte  store 32-, 16- or 8-bit numbers",
    ".zero n           reserve n bytes that start at 0",
    ".asciz \"...\"      store text followed by a 0 byte (a C string)",
    ".globl NAME       make NAME visible outside this file (the firmware calls it)",
    ".type / .size     tell the linker and debuggers that NAME is a function and how long it is",
]


def asm_symbol(name: str) -> str:
    """The assembly name of a student variable: the C name, unless it looks like a register."""
    return f"{name}_var" if name in REGISTER_NAMES else name


def asm_string(value: Any) -> str:
    """Escape text for a .asciz directive; non-ASCII bytes become octal escapes, like in C."""
    out = []
    for byte in str(value if value is not None else "").encode("utf-8"):
        if byte == 0x5C:
            out.append("\\\\")
        elif byte == 0x22:
            out.append('\\"')
        elif 32 <= byte < 127:
            out.append(chr(byte))
        else:
            out.append(f"\\{byte:03o}")
    return "".join(out)


def asm_note(text: Any) -> str:
    """Make text safe for a '#' comment after an instruction in a preprocessed .S file."""
    text = c_comment(text).replace("//", "/ /").replace("\\", "/")
    return text.rstrip()


_RTL_BINARY = {"add": "+", "sub": "-", "mul": "*", "div": "/", "divu": "/ (unsigned)", "rem": "%", "remu": "% (unsigned)",
               "and": "&", "or": "|", "xor": "^", "sll": "<<", "srl": ">> (logical)", "sra": ">> (arithmetic)",
               "addi": "+", "andi": "&", "ori": "|", "xori": "^", "slli": "<<", "srli": ">> (logical)",
               "srai": ">> (arithmetic)"}
_RTL_BRANCH = {"beq": "==", "bne": "!=", "blt": "<", "bge": ">=", "bgt": ">", "ble": "<=", "bltu": "< (unsigned)",
               "bgeu": ">= (unsigned)", "bgtu": "> (unsigned)", "bleu": "<= (unsigned)"}
_RTL_ZERO_BRANCH = {"beqz": "==", "bnez": "!=", "bltz": "<", "bgez": ">=", "bgtz": ">", "blez": "<="}
_RTL_LOADS = {"lw": "word", "lh": "signed halfword", "lhu": "halfword", "lb": "signed byte", "lbu": "byte"}
_RTL_STORES = {"sw": "word", "sh": "halfword", "sb": "byte"}


def describe_instruction(code: str) -> str:
    """What one instruction does, written as a register transfer (rd = rs1 + rs2)."""
    parts = code.split(None, 1)
    op = parts[0]
    args = [arg.strip() for arg in parts[1].split(",")] if len(parts) > 1 else []
    if op in _RTL_BINARY and len(args) == 3:
        return f"{args[0]} = {args[1]} {_RTL_BINARY[op]} {args[2]}"
    if op in _RTL_BRANCH and len(args) == 3:
        return f"if {args[0]} {_RTL_BRANCH[op]} {args[1]} go to {args[2]}"
    if op in _RTL_ZERO_BRANCH and len(args) == 2:
        return f"if {args[0]} {_RTL_ZERO_BRANCH[op]} 0 go to {args[1]}"
    if op in _RTL_LOADS and len(args) == 2:
        return f"{args[0]} = {_RTL_LOADS[op]} at {args[1]}"
    if op in _RTL_STORES and len(args) == 2:
        return f"{_RTL_STORES[op]} at {args[1]} = {args[0]}"
    simple = {
        "li": lambda a: f"{a[0]} = {a[1]}",
        "la": lambda a: f"{a[0]} = address of {a[1]}",
        "mv": lambda a: f"{a[0]} = {a[1]}",
        "neg": lambda a: f"{a[0]} = -{a[1]}",
        "not": lambda a: f"{a[0]} = ~{a[1]}",
        "seqz": lambda a: f"{a[0]} = ({a[1]} == 0)",
        "snez": lambda a: f"{a[0]} = ({a[1]} != 0)",
        "sltz": lambda a: f"{a[0]} = ({a[1]} < 0)",
        "sgtz": lambda a: f"{a[0]} = ({a[1]} > 0)",
        "slt": lambda a: f"{a[0]} = ({a[1]} < {a[2]})",
        "sltu": lambda a: f"{a[0]} = ({a[1]} < {a[2]}, unsigned)",
        "j": lambda a: f"go to {a[0]}",
        "call": lambda a: f"call {a[0]}",
        "tail": lambda a: f"tail call {a[0]}",
        "ret": lambda a: "return to the caller",
    }
    try:
        return simple[op](args) if op in simple else ""
    except IndexError:
        return ""


def asm_line(code: str, note: str = "") -> str:
    """One line of assembly: label or indented instruction, then an aligned '# comment'.

    An instruction without a note gets its register-transfer description, so every
    instruction in the file is explained.
    """
    if code.endswith(":"):
        text = code
    else:
        parts = code.split(None, 1)
        text = "    " + (parts[0].ljust(5) + " " + parts[1] if len(parts) > 1 else parts[0])
        if not note and not code.startswith("."):
            note = describe_instruction(code)
    if note:
        text = text.ljust(ASM_COMMENT_COLUMN - 1) + " # " + asm_note(note)
    return text


def comment_block(lines: list[str], indent: str = "", width: int = _WRAP) -> list[str]:
    """Wrap paragraphs into one /* ... */ comment. A line starting with '|' is kept as it is."""
    body: list[str] = []
    for line in lines:
        if line.startswith("|"):
            body.append(c_comment(line[1:]))
        else:
            body.extend(textwrap.wrap(c_comment(line), max(30, width - len(indent) - 3)) or [""])
    if len(body) == 1:
        return [f"{indent}/* {body[0]} */"]
    return [f"{indent}/* {body[0]}", *(f"{indent} * {line}".rstrip() for line in body[1:-1]), f"{indent} * {body[-1]} */"]


def asm_rows(rows: list[Any]) -> list[str]:
    """Lay out hand-written assembly: strings are copied, (code, note) pairs are formatted."""
    return [row if isinstance(row, str) else asm_line(*row) for row in rows]


def _doc(*lines: str) -> list[str]:
    return ["/*", *(f" * {line}".rstrip() for line in lines), " */"]


def _function(name: str, doc: list[str], rows: list[Any]) -> list[str]:
    """A helper function: documentation, symbol type, label, body, size."""
    return [*doc, f"    .type {name}, @function", f"{name}:", *asm_rows(rows), f"    .size {name}, .-{name}", ""]


def _save(registers: list[str], frame: int) -> list[tuple[str, str]]:
    """Prologue: make a stack frame and save ra and the callee-saved registers we use."""
    rows = [(f"addi sp, sp, -{frame}", f"make a {frame}-byte stack frame (sp stays a multiple of 16)"),
            (f"sw ra, {frame - 4}(sp)", "save the return address: our own calls overwrite ra")]
    for index, reg in enumerate(registers):
        rows.append((f"sw {reg}, {frame - 8 - 4 * index}(sp)", f"save {reg}: the caller expects it unchanged"))
    return rows


def _restore(registers: list[str], frame: int) -> list[tuple[str, str]]:
    rows = [(f"lw {reg}, {frame - 8 - 4 * index}(sp)", f"restore {reg}") for index, reg in enumerate(registers)]
    return rows + [(f"lw ra, {frame - 4}(sp)", "restore the return address"),
                   (f"addi sp, sp, {frame}", "free the stack frame"),
                   ("ret", "return to the caller")]


def _clamp_register(reg: str, label: str) -> list[Any]:
    return [(f"bgez {reg}, {label}_not_negative", f"{reg} >= 0: no change"),
            (f"li {reg}, 0", "below 0: use 0"),
            (f"{label}_not_negative:", ""),
            (f"ble {reg}, t0, {label}_done", f"{reg} <= 255: no change"),
            (f"mv {reg}, t0", "above 255: use 255"),
            (f"{label}_done:", "")]


def _circle_points(filled: bool) -> list[Any]:
    """The body of the circle loop: four bars (filled) or eight mirrored pixels."""
    rows: list[Any] = []
    if filled:
        bars = [("s4", "s5", "+", "bar below the centre: y = cy + y, x from cx - x"),
                ("s4", "s5", "-", "bar above the centre: y = cy - y"),
                ("s5", "s4", "+", "bar near the bottom: y = cy + x, x from cx - y"),
                ("s5", "s4", "-", "bar near the top: y = cy - x")]
        for half, step, sign, note in bars:
            rows += [f"    /* {note} */",
                     (f"sub a0, s0, {half}", "a0 = left end of the bar"),
                     (f"{'add' if sign == '+' else 'sub'} a1, s1, {step}", "a1 = the bar's row"),
                     (f"slli a2, {half}, 1", "a2 = 2 * half-width ..."),
                     ("addi a2, a2, 1", "... + 1 = the bar's width"),
                     ("li a3, 1", "a3 = height 1"),
                     ("mv a4, s2", "a4 = colour"),
                     ("call prg32_gfx_rect", "draw the bar")]
        return rows
    for dx, dy, sx, sy in (("s4", "s5", "+", "+"), ("s4", "s5", "-", "+"), ("s4", "s5", "+", "-"), ("s4", "s5", "-", "-"),
                           ("s5", "s4", "+", "+"), ("s5", "s4", "-", "+"), ("s5", "s4", "+", "-"), ("s5", "s4", "-", "-")):
        name_x, name_y = ("x" if dx == "s4" else "y"), ("y" if dy == "s5" else "x")
        rows += [(f"{'add' if sx == '+' else 'sub'} a0, s0, {dx}", f"a0 = cx {sx} {name_x}"),
                 (f"{'add' if sy == '+' else 'sub'} a1, s1, {dy}", f"a1 = cy {sy} {name_y}"),
                 ("mv a2, s2", "a2 = colour"),
                 ("call prg32_gfx_pixel", f"pixel (cx {sx} {name_x}, cy {sy} {name_y})")]
    return rows


# Hand-written assembly helpers, one per C helper with the same name and contract.
# Registers: inputs arrive in a0, a1, ...; the result leaves in a0. A helper may
# change t0-t6 and a0-a7 freely; it must give s0-s11, sp and ra back unchanged.
_LINE_SAVED = ["s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10"]
ASM_HELPERS: dict[str, list[str]] = {
    "list_get": _function("prg32_kit_get", _doc(
        "int prg32_kit_get(int *list, int size, int index)       a0 = list, a1 = size, a2 = index",
        "Result a0: list[index], or 0 when index is outside 0..size-1.",
        "One unsigned comparison checks both ends: as an unsigned number a negative",
        "index looks enormous (-1 is 0xFFFFFFFF), so it is never below size."), [
        ("bgeu a2, a1, .Lkit_get_outside", "index outside 0..size-1 (unsigned test)"),
        ("slli a2, a2, 2", "byte offset = index * 4 (an int is 4 bytes)"),
        ("add a0, a0, a2", "a0 = address of list[index]"),
        ("lw a0, 0(a0)", "a0 = list[index]"),
        ("ret", "return the item"),
        (".Lkit_get_outside:", ""),
        ("li a0, 0", "outside the list the answer is 0"),
        ("ret", ""),
    ]),
    "list_set": _function("prg32_kit_set", _doc(
        "void prg32_kit_set(int *list, int size, int index, int value)",
        "a0 = list, a1 = size, a2 = index, a3 = value. Writes only inside the list."), [
        ("bgeu a2, a1, .Lkit_set_outside", "index outside 0..size-1: write nothing"),
        ("slli a2, a2, 2", "byte offset = index * 4"),
        ("add a0, a0, a2", "a0 = address of list[index]"),
        ("sw a3, 0(a0)", "list[index] = value"),
        (".Lkit_set_outside:", ""),
        ("ret", ""),
    ]),
    "list_fill": _function("prg32_kit_fill", _doc(
        "void prg32_kit_fill(int *list, int size, int value)     a0 = list, a1 = size, a2 = value",
        "Walks the list with a pointer: a0 moves 4 bytes per item, a1 counts the items left."), [
        (".Lkit_fill_loop:", ""),
        ("blez a1, .Lkit_fill_done", "no items left: stop"),
        ("sw a2, 0(a0)", "*a0 = value"),
        ("addi a0, a0, 4", "next item"),
        ("addi a1, a1, -1", "one item fewer to go"),
        ("j .Lkit_fill_loop", ""),
        (".Lkit_fill_done:", ""),
        ("ret", ""),
    ]),
    "abs": _function("prg32_kit_abs", _doc(
        "int prg32_kit_abs(int value)                              a0 = value",
        "A branch-free absolute value. t0 is 0 for a positive value and -1 (all ones)",
        "for a negative one; (value XOR t0) - t0 is then value, or ~value + 1 = -value",
        "(two's complement negation)."), [
        ("srai t0, a0, 31", "t0 = 0 if value >= 0, -1 if value < 0 (copies of the sign bit)"),
        ("xor a0, a0, t0", "negative: flip every bit"),
        ("sub a0, a0, t0", "negative: and add 1"),
        ("ret", ""),
    ]),
    "sign": _function("prg32_kit_sign", _doc(
        "int prg32_kit_sign(int value): -1, 0 or 1                 a0 = value"), [
        ("sgtz t0, a0", "t0 = 1 if value > 0"),
        ("sltz t1, a0", "t1 = 1 if value < 0"),
        ("sub a0, t0, t1", "1 - 0, 0 - 0 or 0 - 1"),
        ("ret", ""),
    ]),
    "min": _function("prg32_kit_min", _doc("int prg32_kit_min(int a, int b)                          a0 = a, a1 = b"), [
        ("blt a0, a1, .Lkit_min_done", "a < b: a is already in a0"),
        ("mv a0, a1", "otherwise the answer is b"),
        (".Lkit_min_done:", ""),
        ("ret", ""),
    ]),
    "max": _function("prg32_kit_max", _doc("int prg32_kit_max(int a, int b)                          a0 = a, a1 = b"), [
        ("blt a1, a0, .Lkit_max_done", "a > b: a is already in a0"),
        ("mv a0, a1", "otherwise the answer is b"),
        (".Lkit_max_done:", ""),
        ("ret", ""),
    ]),
    "clamp": _function("prg32_kit_clamp", _doc(
        "int prg32_kit_clamp(int value, int low, int high)        a0 = value, a1 = low, a2 = high"), [
        ("bge a0, a1, .Lkit_clamp_not_low", "value >= low: not too small"),
        ("mv a0, a1", "too small: the answer is low"),
        ("ret", ""),
        (".Lkit_clamp_not_low:", ""),
        ("bge a2, a0, .Lkit_clamp_done", "value <= high: not too large"),
        ("mv a0, a2", "too large: the answer is high"),
        (".Lkit_clamp_done:", ""),
        ("ret", ""),
    ]),
    "sin": _function("prg32_kit_sin", _doc(
        "int prg32_kit_sin(int degrees): 256 * sine, from -256 to 256    a0 = degrees",
        "prg32_kit_sine_table holds 91 halfwords (2 bytes each) for 0..90 degrees; entry d",
        "is at byte offset 2 * d. The other quarters mirror the first one (see the C file)."), [
        ("li t0, 360", "t0 = one full turn"),
        ("rem a0, a0, t0", "a = degrees % 360, from -359 to 359"),
        ("bgez a0, .Lkit_sin_turned", ""),
        ("add a0, a0, t0", "negative: add a turn, now 0..359"),
        (".Lkit_sin_turned:", ""),
        ("la t1, prg32_kit_sine_table", "t1 = address of the table"),
        ("li t2, 90", ""),
        ("bgt a0, t2, .Lkit_sin_second", "a > 90: not the first quarter"),
        ("slli t3, a0, 1", "first quarter: offset = 2 * a"),
        ("add t3, t1, t3", ""),
        ("lh a0, 0(t3)", "a0 = table[a]"),
        ("ret", ""),
        (".Lkit_sin_second:", ""),
        ("li t2, 180", ""),
        ("bgt a0, t2, .Lkit_sin_third", "a > 180: not the second quarter"),
        ("sub t3, t2, a0", "second quarter: table[180 - a]"),
        ("slli t3, t3, 1", ""),
        ("add t3, t1, t3", ""),
        ("lh a0, 0(t3)", ""),
        ("ret", ""),
        (".Lkit_sin_third:", ""),
        ("li t2, 270", ""),
        ("bgt a0, t2, .Lkit_sin_fourth", "a > 270: the fourth quarter"),
        ("addi t3, a0, -180", "third quarter: -table[a - 180]"),
        ("slli t3, t3, 1", ""),
        ("add t3, t1, t3", ""),
        ("lh a0, 0(t3)", ""),
        ("neg a0, a0", "below the axis: negative"),
        ("ret", ""),
        (".Lkit_sin_fourth:", ""),
        ("sub t3, t0, a0", "fourth quarter: -table[360 - a] (t0 is still 360)"),
        ("slli t3, t3, 1", ""),
        ("add t3, t1, t3", ""),
        ("lh a0, 0(t3)", ""),
        ("neg a0, a0", ""),
        ("ret", ""),
    ]),
    "cos": _function("prg32_kit_cos", _doc(
        "int prg32_kit_cos(int degrees) = prg32_kit_sin(degrees % 360 + 90)   a0 = degrees",
        "A tail call: sin's own ret goes straight back to our caller."), [
        ("li t0, 360", ""),
        ("rem a0, a0, t0", "degrees % 360"),
        ("addi a0, a0, 90", "+ 90: cosine is the sine a quarter turn later"),
        ("j prg32_kit_sin", "tail call: jump, do not call"),
    ]),
    "rgb": _function("prg32_kit_rgb", _doc(
        "int prg32_kit_rgb(int r, int g, int b): one RGB565 colour     a0 = r, a1 = g, a2 = b",
        "Each part is first kept inside 0..255, then the top bits are moved into place:",
        "red -> bits 15..11, green -> bits 10..5, blue -> bits 4..0."), [
        ("li t0, 255", "t0 = the largest allowed value"),
        *_clamp_register("a0", ".Lkit_rgb_red"),
        *_clamp_register("a1", ".Lkit_rgb_green"),
        *_clamp_register("a2", ".Lkit_rgb_blue"),
        ("andi a0, a0, 0xF8", "red: keep the top 5 of 8 bits"),
        ("slli a0, a0, 8", "move them to bits 15..11"),
        ("andi a1, a1, 0xFC", "green: keep the top 6 bits"),
        ("slli a1, a1, 3", "move them to bits 10..5"),
        ("srli a2, a2, 3", "blue: the top 5 bits become bits 4..0"),
        ("or a0, a0, a1", "join red and green"),
        ("or a0, a0, a2", "and blue"),
        ("ret", ""),
    ]),
    "random": _function("prg32_kit_random", _doc(
        "int prg32_kit_random(int low, int high), both included        a0 = low, a1 = high",
        "Calls the firmware, so it needs a stack frame: the call overwrites ra, and every",
        "a- and t-register may change inside the firmware, so low waits on the stack."), [
        ("bgt a1, a0, .Lkit_random_range", "high > low: a real range"),
        ("ret", "otherwise the answer is low (already in a0)"),
        (".Lkit_random_range:", ""),
        ("addi sp, sp, -16", "stack frame: ra and low"),
        ("sw ra, 12(sp)", ""),
        ("sw a0, 8(sp)", "keep low safe during the call"),
        ("sub a1, a1, a0", "a1 = high - low (the largest offset)"),
        ("li a0, 0", "a0 = 0 (the smallest offset)"),
        ("call prg32_random_number", "a0 = random offset, 0..high-low"),
        ("lw t0, 8(sp)", "t0 = low"),
        ("add a0, a0, t0", "result = low + offset"),
        ("lw ra, 12(sp)", ""),
        ("addi sp, sp, 16", ""),
        ("ret", ""),
    ]),
    "frame": _function("prg32_kit_frame", _doc("int prg32_kit_frame(void): frames finished since the start"), [
        ("la t0, prg32_kit_frame_count", ""),
        ("lw a0, 0(t0)", "a0 = the frame counter"),
        ("ret", ""),
    ]),
    "tile": _function("prg32_kit_tile", _doc(
        "int prg32_kit_tile(int layer, int tx, int ty)        a0 = layer, a1 = column, a2 = row",
        "Values outside 0..255 answer 0. Otherwise a tail call to the firmware: its",
        "result (a uint8_t, already zero-extended by the firmware) is ours."), [
        ("li t0, 255", ""),
        ("bgtu a0, t0, .Lkit_tile_outside", "unsigned: catches < 0 and > 255 at once"),
        ("bgtu a1, t0, .Lkit_tile_outside", ""),
        ("bgtu a2, t0, .Lkit_tile_outside", ""),
        ("tail prg32_playfield_get", "the firmware returns directly to our caller"),
        (".Lkit_tile_outside:", ""),
        ("li a0, 0", ""),
        ("ret", ""),
    ]),
    "peer_value": _function("prg32_kit_peer_value", _doc(
        "int prg32_kit_peer_value(int index, int what)        a0 = player index, a1 = 0 x / 1 y / 2 look",
        "The firmware fills a prg32_player_state_t that lives in our stack frame:",
        "|   offset 0  uint32_t player_id    offset 4  int16_t x    offset 6  int16_t y",
        "|   offset 8  uint16_t sprite       offset 10 uint16_t flags  ...  24 bytes in all",
        "lh sign-extends the signed fields, lhu zero-extends the unsigned one."), [
        ("addi sp, sp, -32", "frame: 24-byte structure, what, ra"),
        ("sw ra, 28(sp)", ""),
        ("sw a1, 24(sp)", "keep 'what' during the call"),
        ("mv a1, sp", "a1 = &peer: the structure starts at 0(sp)"),
        ("call prg32_multiplayer_get_peer", "a0 = 0 when the firmware filled it"),
        ("bnez a0, .Lkit_peer_none", "no such player"),
        ("lw t0, 24(sp)", "t0 = what"),
        ("beqz t0, .Lkit_peer_x", ""),
        ("li t1, 1", ""),
        ("beq t0, t1, .Lkit_peer_y", ""),
        ("lhu a0, 8(sp)", "a0 = peer.sprite"),
        ("j .Lkit_peer_done", ""),
        (".Lkit_peer_x:", ""),
        ("lh a0, 4(sp)", "a0 = peer.x"),
        ("j .Lkit_peer_done", ""),
        (".Lkit_peer_y:", ""),
        ("lh a0, 6(sp)", "a0 = peer.y"),
        ("j .Lkit_peer_done", ""),
        (".Lkit_peer_none:", ""),
        ("li a0, 0", ""),
        (".Lkit_peer_done:", ""),
        ("lw ra, 28(sp)", ""),
        ("addi sp, sp, 32", ""),
        ("ret", ""),
    ]),
    "peers": _function("prg32_kit_peers", _doc("int prg32_kit_peers(void): other players connected"), [
        ("tail prg32_multiplayer_get_peer_count", "the firmware answers directly to our caller"),
    ]),
    "peer_x": _function("prg32_kit_peer_x", _doc("int prg32_kit_peer_x(int index)"), [
        ("li a1, 0", "what = 0: x"), ("j prg32_kit_peer_value", "tail call")]),
    "peer_y": _function("prg32_kit_peer_y", _doc("int prg32_kit_peer_y(int index)"), [
        ("li a1, 1", "what = 1: y"), ("j prg32_kit_peer_value", "tail call")]),
    "peer_look": _function("prg32_kit_peer_look", _doc("int prg32_kit_peer_look(int index)"), [
        ("li a1, 2", "what = 2: look"), ("j prg32_kit_peer_value", "tail call")]),
    "beep": _function("prg32_kit_note_from_hz", _doc(
        "uint8_t prg32_kit_note_from_hz(int hz): nearest MIDI note, 48..84     a0 = hz",
        "prg32_kit_note_frequencies holds the 37 frequencies of notes 48..84 as halfwords.",
        "t0 walks the table; the first midpoint above hz picks the note."), [
        ("la t0, prg32_kit_note_frequencies", "t0 = &frequencies[0]"),
        ("lhu t1, 0(t0)", "t1 = 131 Hz (C3)"),
        ("bgt a0, t1, .Lkit_note_search", ""),
        ("li a0, 48", "at or below C3: note 48"),
        ("ret", ""),
        (".Lkit_note_search:", ""),
        ("li t2, 0", "t2 = index"),
        ("li t6, 36", "t6 = number of neighbour pairs"),
        (".Lkit_note_loop:", ""),
        ("bge t2, t6, .Lkit_note_top", "every pair tried"),
        ("lhu t3, 0(t0)", "t3 = frequencies[index]"),
        ("lhu t4, 2(t0)", "t4 = frequencies[index + 1]"),
        ("add t3, t3, t4", ""),
        ("srli t3, t3, 1", "t3 = midpoint (the sum is positive: shift = divide by 2)"),
        ("blt a0, t3, .Lkit_note_found", "below the midpoint: found"),
        ("addi t0, t0, 2", "next halfword"),
        ("addi t2, t2, 1", "index + 1"),
        ("j .Lkit_note_loop", ""),
        (".Lkit_note_found:", ""),
        ("addi a0, t2, 48", "note = 48 + index"),
        ("ret", ""),
        (".Lkit_note_top:", ""),
        ("li a0, 84", "above every midpoint: C6"),
        ("ret", ""),
    ]),
    "number": [
        *_function("prg32_kit_format_number", _doc(
            "int prg32_kit_format_number(char *out, int value): decimal text    a0 = out, a1 = value",
            "Result a0: the number of characters written (a 0 byte follows them).",
            "The digits come out last-first (value % 10), so they wait in a 16-byte buffer on",
            "the stack (C: char digits[11]) and are then copied to out in reverse order.",
            "|   t0 magnitude   t1 count   t2 ten   t3 digit   t4 address   t5 write pointer"), [
            ("addi sp, sp, -16", "the digit buffer lives in this frame"),
            ("mv t0, a1", "t0 = magnitude"),
            ("bgez a1, .Lkit_format_positive", ""),
            ("neg t0, a1", "negative: magnitude = -value (as unsigned, also for -2147483648)"),
            (".Lkit_format_positive:", ""),
            ("li t1, 0", "count = 0"),
            ("li t2, 10", "t2 = 10, the base (and the most digits we keep)"),
            (".Lkit_format_digit:", ""),
            ("remu t3, t0, t2", "t3 = magnitude % 10, the last digit"),
            ("addi t3, t3, '0'", "digit -> character ('0' is 48)"),
            ("add t4, sp, t1", "t4 = &digits[count]"),
            ("sb t3, 0(t4)", "digits[count] = character"),
            ("addi t1, t1, 1", "count++"),
            ("divu t0, t0, t2", "magnitude /= 10"),
            ("beqz t0, .Lkit_format_copy", "no digits left"),
            ("blt t1, t2, .Lkit_format_digit", "and at most 10 digits"),
            (".Lkit_format_copy:", ""),
            ("mv t5, a0", "t5 = where the next character goes"),
            ("bgez a1, .Lkit_format_reverse", ""),
            ("li t3, '-'", "negative: a minus sign first"),
            ("sb t3, 0(t5)", ""),
            ("addi t5, t5, 1", ""),
            (".Lkit_format_reverse:", ""),
            ("addi t1, t1, -1", "--count"),
            ("add t4, sp, t1", ""),
            ("lbu t3, 0(t4)", "t3 = digits[count]"),
            ("sb t3, 0(t5)", "copy it, last digit first"),
            ("addi t5, t5, 1", ""),
            ("bnez t1, .Lkit_format_reverse", "until every digit is copied"),
            ("sb zero, 0(t5)", "the 0 byte that ends the text"),
            ("sub a0, t5, a0", "length = end - start"),
            ("addi sp, sp, 16", ""),
            ("ret", ""),
        ]),
        *_function("prg32_kit_draw_number", _doc(
            "void prg32_kit_draw_number(int x, int y, int value, uint16_t fg, uint16_t bg)",
            "a0 = x, a1 = y, a2 = value, a3 = fg, a4 = bg. The text buffer (C: char text[12])",
            "is the first 12 bytes of the stack frame."), [
            ("addi sp, sp, -32", "frame: text[12], x, y, fg, bg, ra"),
            ("sw ra, 28(sp)", ""),
            ("sw a0, 12(sp)", "keep x"),
            ("sw a1, 16(sp)", "keep y"),
            ("sw a3, 20(sp)", "keep fg"),
            ("sw a4, 24(sp)", "keep bg"),
            ("mv a1, a2", "a1 = value"),
            ("mv a0, sp", "a0 = text (the buffer at 0(sp))"),
            ("call prg32_kit_format_number", "number -> text"),
            ("lw a0, 12(sp)", "a0 = x"),
            ("lw a1, 16(sp)", "a1 = y"),
            ("mv a2, sp", "a2 = text"),
            ("lw a3, 20(sp)", "a3 = fg"),
            ("lw a4, 24(sp)", "a4 = bg"),
            ("call prg32_gfx_text8", "text -> screen"),
            ("lw ra, 28(sp)", ""),
            ("addi sp, sp, 32", ""),
            ("ret", ""),
        ]),
    ],
    "band": _function("prg32_kit_band", _doc(
        "void prg32_kit_band(const char *text, int has_value, int value)   a0 = text, a1 = has, a2 = value",
        "Builds the band text in prg32_kit_band_now and calls the firmware only when it",
        "differs from prg32_kit_band_shown.",
        "|   t0 = band_now   t1 = length   t2 = band_shown   t3, t4, t5 = scratch"), [
        ("addi sp, sp, -16", "frame: ra and the saved length"),
        ("sw ra, 12(sp)", ""),
        ("la t0, prg32_kit_band_now", ""),
        ("li t1, 0", "length = 0"),
        ("li t2, 32", "copy at most 32 characters"),
        (".Lkit_band_copy:", ""),
        ("add t3, a0, t1", ""),
        ("lbu t4, 0(t3)", "t4 = text[length]"),
        ("beqz t4, .Lkit_band_copied", "end of text"),
        ("bge t1, t2, .Lkit_band_copied", "32 characters copied"),
        ("add t3, t0, t1", ""),
        ("sb t4, 0(t3)", "band_now[length] = text[length]"),
        ("addi t1, t1, 1", "++length"),
        ("j .Lkit_band_copy", ""),
        (".Lkit_band_copied:", ""),
        ("beqz a1, .Lkit_band_end", "no number to add"),
        ("add t3, t0, t1", ""),
        ("li t4, ' '", ""),
        ("sb t4, 0(t3)", "a space before the number"),
        ("addi t1, t1, 1", ""),
        ("sw t1, 8(sp)", "keep length: the call may change t-registers"),
        ("add a0, t0, t1", "a0 = where the digits go"),
        ("mv a1, a2", "a1 = value"),
        ("call prg32_kit_format_number", "a0 = digits written"),
        ("lw t1, 8(sp)", ""),
        ("add t1, t1, a0", "length += digits"),
        ("la t0, prg32_kit_band_now", "t0 again (t-registers do not survive calls)"),
        (".Lkit_band_end:", ""),
        ("add t3, t0, t1", ""),
        ("sb zero, 0(t3)", "band_now[length] = 0"),
        ("la t2, prg32_kit_band_shown", ""),
        ("li t3, 0", "index = 0"),
        (".Lkit_band_compare:", ""),
        ("bgt t3, t1, .Lkit_band_same", "every character equal: nothing to do"),
        ("add t4, t0, t3", ""),
        ("lbu t4, 0(t4)", "t4 = band_now[index]"),
        ("add t5, t2, t3", ""),
        ("lbu t5, 0(t5)", "t5 = band_shown[index]"),
        ("bne t4, t5, .Lkit_band_changed", "first difference"),
        ("addi t3, t3, 1", ""),
        ("j .Lkit_band_compare", ""),
        (".Lkit_band_changed:", ""),
        ("li t3, 0", "index = 0"),
        (".Lkit_band_store:", ""),
        ("add t4, t0, t3", ""),
        ("lbu t4, 0(t4)", ""),
        ("add t5, t2, t3", ""),
        ("sb t4, 0(t5)", "band_shown[index] = band_now[index]"),
        ("addi t3, t3, 1", ""),
        ("ble t3, t1, .Lkit_band_store", "for index = 0 .. length"),
        ("mv a0, t0", "a0 = band_now"),
        ("call prg32_band_set_game_info", "the firmware redraws the band"),
        (".Lkit_band_same:", ""),
        ("lw ra, 12(sp)", ""),
        ("addi sp, sp, 16", ""),
        ("ret", ""),
    ]),
    "frame_rect": _function("prg32_kit_draw_frame", _doc(
        "void prg32_kit_draw_frame(int x, int y, int w, int h, uint16_t color)",
        "Four 1-pixel bars. The inputs move to the callee-saved registers s0..s4, which",
        "keep their values across the four firmware calls.",
        "|   s0 = x   s1 = y   s2 = w   s3 = h   s4 = colour"), [
        ("blez a2, .Lkit_frame_skip", "w <= 0: nothing to draw"),
        ("blez a3, .Lkit_frame_skip", "h <= 0: nothing to draw"),
        *_save(["s0", "s1", "s2", "s3", "s4"], 32),
        ("mv s0, a0", ""), ("mv s1, a1", ""), ("mv s2, a2", ""), ("mv s3, a3", ""), ("mv s4, a4", ""),
        "    /* top edge: prg32_gfx_rect(x, y, w, 1, color) */",
        ("mv a0, s0", ""), ("mv a1, s1", ""), ("mv a2, s2", ""), ("li a3, 1", ""), ("mv a4, s4", ""),
        ("call prg32_gfx_rect", ""),
        "    /* bottom edge: prg32_gfx_rect(x, y + h - 1, w, 1, color) */",
        ("mv a0, s0", ""), ("add a1, s1, s3", "y + h"), ("addi a1, a1, -1", "y + h - 1"),
        ("mv a2, s2", ""), ("li a3, 1", ""), ("mv a4, s4", ""),
        ("call prg32_gfx_rect", ""),
        "    /* left edge: prg32_gfx_rect(x, y, 1, h, color) */",
        ("mv a0, s0", ""), ("mv a1, s1", ""), ("li a2, 1", ""), ("mv a3, s3", ""), ("mv a4, s4", ""),
        ("call prg32_gfx_rect", ""),
        "    /* right edge: prg32_gfx_rect(x + w - 1, y, 1, h, color) */",
        ("add a0, s0, s2", "x + w"), ("addi a0, a0, -1", "x + w - 1"),
        ("mv a1, s1", ""), ("li a2, 1", ""), ("mv a3, s3", ""), ("mv a4, s4", ""),
        ("call prg32_gfx_rect", ""),
        *_restore(["s0", "s1", "s2", "s3", "s4"], 32),
        (".Lkit_frame_skip:", ""),
        ("ret", ""),
    ]),
    "line": _function("prg32_kit_draw_line", _doc(
        "void prg32_kit_draw_line(int x1, int y1, int x2, int y2, uint16_t color): Bresenham",
        "Every value lives in a callee-saved register, so the pixel calls cannot change it:",
        "|   s0 = x1   s1 = y1   s2 = x2   s3 = y2   s4 = colour",
        "|   s5 = dx   s6 = dy   s7 = sx   s8 = sy   s9 = error   s10 = steps   t0 = twice"), [
        *_save(_LINE_SAVED, 48),
        ("mv s0, a0", "x1"), ("mv s1, a1", "y1"), ("mv s2, a2", "x2"), ("mv s3, a3", "y2"), ("mv s4, a4", "colour"),
        ("blt s0, s2, .Lkit_line_dx_right", "dx = x2 > x1 ? x2 - x1 : x1 - x2"),
        ("sub s5, s0, s2", ""),
        ("j .Lkit_line_dy", ""),
        (".Lkit_line_dx_right:", ""),
        ("sub s5, s2, s0", ""),
        (".Lkit_line_dy:", ""),
        ("blt s1, s3, .Lkit_line_dy_down", "dy = y2 > y1 ? y1 - y2 : y2 - y1 (negative)"),
        ("sub s6, s3, s1", ""),
        ("j .Lkit_line_sx", ""),
        (".Lkit_line_dy_down:", ""),
        ("sub s6, s1, s3", ""),
        (".Lkit_line_sx:", ""),
        ("li s7, 1", "sx = x1 < x2 ? 1 : -1"),
        ("blt s0, s2, .Lkit_line_sy", ""),
        ("li s7, -1", ""),
        (".Lkit_line_sy:", ""),
        ("li s8, 1", "sy = y1 < y2 ? 1 : -1"),
        ("blt s1, s3, .Lkit_line_start", ""),
        ("li s8, -1", ""),
        (".Lkit_line_start:", ""),
        ("add s9, s5, s6", "error = dx + dy"),
        ("li s10, 0", "steps = 0"),
        (".Lkit_line_step:", ""),
        ("li t0, 2048", ""),
        ("bge s10, t0, .Lkit_line_done", "while (steps++ < 2048)"),
        ("addi s10, s10, 1", ""),
        ("mv a0, s0", ""), ("mv a1, s1", ""), ("mv a2, s4", ""),
        ("call prg32_gfx_pixel", "draw (x1, y1)"),
        ("bne s0, s2, .Lkit_line_move", "not at the end yet"),
        ("beq s1, s3, .Lkit_line_done", "x1 == x2 and y1 == y2: stop"),
        (".Lkit_line_move:", ""),
        ("slli t0, s9, 1", "twice = 2 * error"),
        ("blt t0, s6, .Lkit_line_no_x", "if (twice >= dy)"),
        ("add s9, s9, s6", "error += dy"),
        ("add s0, s0, s7", "x1 += sx"),
        (".Lkit_line_no_x:", ""),
        ("bgt t0, s5, .Lkit_line_step", "if (twice <= dx)"),
        ("add s9, s9, s5", "error += dx"),
        ("add s1, s1, s8", "y1 += sy"),
        ("j .Lkit_line_step", ""),
        (".Lkit_line_done:", ""),
        *_restore(_LINE_SAVED, 48),
    ]),
    "circle": _function("prg32_kit_draw_circle", _doc(
        "void prg32_kit_draw_circle(int cx, int cy, int r, uint16_t color, int filled)",
        "The midpoint circle algorithm (see the C file).",
        "|   s0 = cx   s1 = cy   s2 = colour   s3 = filled   s4 = x   s5 = y   s6 = error"), [
        ("bltz a2, .Lkit_circle_skip", "no circle with a negative radius"),
        *_save(["s0", "s1", "s2", "s3", "s4", "s5", "s6"], 32),
        ("mv s0, a0", "cx"), ("mv s1, a1", "cy"), ("mv s2, a3", "colour"), ("mv s3, a4", "filled"),
        ("li t0, 200", ""),
        ("ble a2, t0, .Lkit_circle_radius", ""),
        ("mv a2, t0", "radius at most 200"),
        (".Lkit_circle_radius:", ""),
        ("mv s4, a2", "x = r"),
        ("li s5, 0", "y = 0"),
        ("li t0, 1", ""),
        ("sub s6, t0, a2", "error = 1 - r"),
        (".Lkit_circle_loop:", ""),
        ("blt s4, s5, .Lkit_circle_done", "while (x >= y)"),
        ("beqz s3, .Lkit_circle_outline", ""),
        *_circle_points(True),
        ("j .Lkit_circle_next", ""),
        (".Lkit_circle_outline:", ""),
        *_circle_points(False),
        (".Lkit_circle_next:", ""),
        ("addi s5, s5, 1", "++y"),
        ("bgez s6, .Lkit_circle_step_x", "error >= 0: also step left"),
        ("slli t0, s5, 1", ""),
        ("addi t0, t0, 1", ""),
        ("add s6, s6, t0", "error += 2 * y + 1"),
        ("j .Lkit_circle_loop", ""),
        (".Lkit_circle_step_x:", ""),
        ("addi s4, s4, -1", "--x"),
        ("sub t0, s5, s4", ""),
        ("slli t0, t0, 1", ""),
        ("addi t0, t0, 1", ""),
        ("add s6, s6, t0", "error += 2 * (y - x) + 1"),
        ("j .Lkit_circle_loop", ""),
        (".Lkit_circle_done:", ""),
        *_restore(["s0", "s1", "s2", "s3", "s4", "s5", "s6"], 32),
        (".Lkit_circle_skip:", ""),
        ("ret", ""),
    ]),
    "bitmap": _function("prg32_kit_draw_bitmap", _doc(
        "void prg32_kit_draw_bitmap(int x, int y, const uint8_t *rows, uint16_t color, int scale)",
        "|   s0 = x   s1 = y   s2 = rows   s3 = colour   s4 = scale   s5 = row   s6 = col"), [
        *_save(["s0", "s1", "s2", "s3", "s4", "s5", "s6"], 32),
        ("mv s0, a0", ""), ("mv s1, a1", ""), ("mv s2, a2", ""), ("mv s3, a3", ""), ("mv s4, a4", ""),
        ("li t0, 1", ""),
        ("bge s4, t0, .Lkit_bitmap_scale_low", ""),
        ("li s4, 1", "scale at least 1"),
        (".Lkit_bitmap_scale_low:", ""),
        ("li t0, 8", ""),
        ("ble s4, t0, .Lkit_bitmap_scale_high", ""),
        ("li s4, 8", "scale at most 8"),
        (".Lkit_bitmap_scale_high:", ""),
        ("li s5, 0", "row = 0"),
        (".Lkit_bitmap_row:", ""),
        ("li s6, 0", "col = 0"),
        (".Lkit_bitmap_col:", ""),
        ("add t0, s2, s5", ""),
        ("lbu t0, 0(t0)", "t0 = rows[row]"),
        ("li t1, 0x80", ""),
        ("srl t1, t1, s6", "t1 = 0x80 >> col: this column's bit"),
        ("and t0, t0, t1", ""),
        ("beqz t0, .Lkit_bitmap_next", "pixel not lit"),
        ("mul t2, s6, s4", ""),
        ("add a0, s0, t2", "a0 = x + col * scale"),
        ("mul t2, s5, s4", ""),
        ("add a1, s1, t2", "a1 = y + row * scale"),
        ("mv a2, s4", "a2 = scale (width)"),
        ("mv a3, s4", "a3 = scale (height)"),
        ("mv a4, s3", "a4 = colour"),
        ("call prg32_gfx_rect", "one square per lit pixel"),
        (".Lkit_bitmap_next:", ""),
        ("addi s6, s6, 1", "next column"),
        ("li t0, 8", ""),
        ("blt s6, t0, .Lkit_bitmap_col", ""),
        ("addi s5, s5, 1", "next row"),
        ("blt s5, t0, .Lkit_bitmap_row", ""),
        *_restore(["s0", "s1", "s2", "s3", "s4", "s5", "s6"], 32),
    ]),
}

# C-only helpers: the assembly has no div/mod helper and keeps "once" as data.
_ASM_HELPER_NEEDS = {"cos": {"sin"}, "band": {"number"}, "peer_x": {"peer_value"}, "peer_y": {"peer_value"},
                     "peer_look": {"peer_value"}}
_ASM_HELPER_ORDER = ["list_get", "list_set", "list_fill", "abs", "sign", "min", "max", "clamp", "sin", "cos", "rgb",
                     "random", "frame", "tile", "peer_value", "peers", "peer_x", "peer_y", "peer_look", "beep",
                     "number", "band", "frame_rect", "line", "circle", "bitmap"]

_NOTE_FREQUENCIES = [131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247, 262, 277, 294, 311, 330, 349, 370,
                     392, 415, 440, 466, 494, 523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988, 1047]


def _leaf(tree: tuple) -> bool:
    while tree[0] == "paren":
        tree = tree[1]
    return tree[0] in {"num", "name", "const"}


def _strip(tree: tuple) -> tuple:
    while tree[0] == "paren":
        tree = tree[1]
    return tree


def _number(text: str) -> int:
    return int(text, 16) if text[:2].lower() == "0x" else int(text, 10)


class _AsmEmitter:
    """Writes one RISC-V function at a time; the C emitter supplies the C line of each block."""

    def __init__(self, game_ir: dict[str, Any]):
        self.c = _Emitter(game_ir, explain=False)
        self.lists = self.c.lists
        self.title = self.c.title
        self.needs: set[str] = set()
        self.constants: set[str] = set()
        self.strings: dict[str, str] = {}
        self.lines: list[str] = []
        self.max_slot = 0
        self.label_count = 0
        self.trace: dict[int, str] | None = None  # see generator.ir_to_c

    # ---- small writers ---------------------------------------------------
    def emit(self, code: str, note: str = "") -> None:
        self.lines.append(asm_line(code, note))

    def new_label(self, hint: str) -> str:
        self.label_count += 1
        return f".L{hint}_{self.label_count}"

    def slot(self, index: int) -> str:
        """Address of stack slot ``index``: 4 bytes each, from the bottom of the frame."""
        self.max_slot = max(self.max_slot, index + 1)
        return f"{4 * index}(sp)"

    def string(self, text: str) -> str:
        if text not in self.strings:
            self.strings[text] = f"prg32_kit_text_{len(self.strings)}"
        return self.strings[text]

    def tree(self, text: Any) -> tuple:
        """Check one IR expression again (like the C emitter) and parse it."""
        checked = check_expr(text, self.lists) if text not in (None, "") else None
        try:
            return parse_expr(checked or "0", self.lists).tree
        except (ExpressionError, RecursionError):
            return ("num", "0")

    def show(self, text: Any) -> str:
        return (check_expr(text, self.lists) if text not in (None, "") else None) or "0"

    def constant(self, name: str) -> str:
        self.constants.add(name)
        return name

    # ---- expressions -----------------------------------------------------
    def load_leaf(self, reg: str, tree: tuple) -> None:
        tree = _strip(tree)
        if tree[0] == "num":
            self.emit(f"li {reg}, {tree[1]}", f"{reg} = {tree[1]}")
        elif tree[0] == "const":
            self.emit(f"li {reg}, {self.constant(tree[1])}", f"{reg} = {tree[1]} ({CONSTANTS[tree[1]]})")
        else:
            symbol = asm_symbol(tree[1])
            self.emit(f"la {reg}, {symbol}", f"{reg} = address of variable {tree[1]}")
            self.emit(f"lw {reg}, 0({reg})", f"{reg} = {tree[1]}")

    def expr(self, tree: tuple, slot: int) -> None:
        """Compute ``tree`` into a0; stack slots from ``slot`` upwards are free to use."""
        tree = _strip(tree)
        kind = tree[0]
        if kind in {"num", "name", "const"}:
            self.load_leaf("a0", tree)
        elif kind == "unary":
            self.expr(tree[2], slot)
            text = render(tree)[:40]
            if tree[1] == "-":
                self.emit("neg a0, a0", f"a0 = {text}")
            elif tree[1] == "!":
                self.emit("seqz a0, a0", f"a0 = {text} (1 when the value was 0)")
            elif tree[1] == "~":
                self.emit("not a0, a0", f"a0 = {text} (every bit flipped)")
        elif kind == "bin":
            self.binary(tree, slot)
        elif kind == "index":
            self.needs.add("list_get")
            self.call("prg32_kit_get", [("addr", asm_symbol(tree[1]), None), ("imm", self.lists[tree[1]], None),
                                        ("expr", tree[2], None)], slot, f"a0 = {render(tree)[:40]}")
        elif kind == "call":
            self.needs.add(tree[1])
            self.call(FUNCTIONS[tree[1]][1], [("expr", arg, None) for arg in tree[2]], slot,
                      f"a0 = {render(tree)[:40]}")
        elif kind == "kitcall":  # a helper call made by a block (not typed by the student)
            self.call(tree[1], [("expr", arg, None) for arg in tree[2]], slot, f"a0 = {tree[3]}")

    def binary(self, tree: tuple, slot: int) -> None:
        op, left, right = tree[1], tree[2], tree[3]
        text = render(tree)[:40]
        if op in {"&&", "||"}:
            decided, end = self.new_label("and_false" if op == "&&" else "or_true"), self.new_label("logic_end")
            self.expr(left, slot)
            if op == "&&":
                self.emit(f"beqz a0, {decided}", "&&: a false left side decides; skip the right side")
            else:
                self.emit(f"bnez a0, {decided}", "||: a true left side decides; skip the right side")
            self.expr(right, slot)
            self.emit("snez a0, a0", f"a0 = {text} (1 or 0)")
            self.emit(f"j {end}", "")
            self.lines.append(asm_line(f"{decided}:"))
            self.emit(f"li a0, {0 if op == '&&' else 1}", f"a0 = {0 if op == '&&' else 1}")
            self.lines.append(asm_line(f"{end}:"))
            return
        self.expr(left, slot)
        if _leaf(right):
            self.load_leaf("t0", right)
            a, b = "a0", "t0"
        else:
            self.emit(f"sw a0, {self.slot(slot)}", f"park the left value in stack slot {slot}")
            self.expr(right, slot + 1)
            self.emit(f"lw t0, {self.slot(slot)}", "t0 = the left value again")
            a, b = "t0", "a0"
        simple = {"+": "add", "-": "sub", "*": "mul", "/": "div", "%": "rem", "&": "and", "|": "or", "^": "xor",
                  "<<": "sll", ">>": "sra"}
        if op in simple:
            extra = {"/": " (RISC-V DIV: rounds toward 0, x / 0 = -1)", "%": " (RISC-V REM: x % 0 = x)",
                     ">>": " (arithmetic shift keeps the sign)"}.get(op, "")
            self.emit(f"{simple[op]} a0, {a}, {b}", f"a0 = {text}{extra}")
        elif op == "<":
            self.emit(f"slt a0, {a}, {b}", f"a0 = {text} (1 or 0)")
        elif op == ">":
            self.emit(f"slt a0, {b}, {a}", f"a0 = {text}: a > b is b < a")
        elif op == "<=":
            self.emit(f"slt a0, {b}, {a}", "a0 = 1 when left > right ...")
            self.emit("xori a0, a0, 1", f"... flipped: a0 = {text}")
        elif op == ">=":
            self.emit(f"slt a0, {a}, {b}", "a0 = 1 when left < right ...")
            self.emit("xori a0, a0, 1", f"... flipped: a0 = {text}")
        elif op in {"==", "!="}:
            self.emit(f"sub a0, {a}, {b}", "a0 = 0 exactly when the two values are equal")
            self.emit(f"{'seqz' if op == '==' else 'snez'} a0, a0", f"a0 = {text} (1 or 0)")

    # ---- calls -----------------------------------------------------------
    def call(self, function: str, args: list[tuple[str, Any, str | None]], slot: int, note: str = "") -> None:
        """Call ``function`` with arguments in a0..a7.

        An argument is ("expr", tree, conversion), ("imm", number or constant name,
        conversion) or ("addr", symbol, None). Arguments that need real work are
        computed first (into a0, or into stack slots when there are several), then
        every register is filled, then narrow parameters are widened by the ABI rules.
        Several computed arguments are evaluated right to left, matching the GCC
        used by the PRG32 toolchain. This matters when two arguments call random().
        """
        work = [i for i, (kind, value, _conv) in enumerate(args) if kind == "expr" and not _leaf(value)]
        if len(work) == 1:
            index = work[0]
            self.expr(args[index][1], slot)
            if index:
                self.emit(f"mv a{index}, a0", f"argument {index + 1} is ready in a{index}")
        elif work:
            for offset, index in enumerate(reversed(work)):
                self.expr(args[index][1], slot + offset)
                self.emit(f"sw a0, {self.slot(slot + offset)}", f"argument {index + 1} waits in stack slot {slot + offset}")
            for offset, index in enumerate(reversed(work)):
                self.emit(f"lw a{index}, {self.slot(slot + offset)}", f"a{index} = argument {index + 1}")
        for index, (kind, value, conversion) in enumerate(args):
            reg = f"a{index}"
            if kind == "expr" and _leaf(value):
                self.load_leaf(reg, value)
            elif kind == "imm":
                if isinstance(value, str):
                    self.emit(f"li {reg}, {self.constant(value)}", f"{reg} = {value}")
                else:
                    self.emit(f"li {reg}, {value}", f"{reg} = {value}")
            elif kind == "addr":
                self.emit(f"la {reg}, {value}", f"{reg} = address of {value}")
            if conversion and not (kind == "imm" or (kind == "expr" and _strip(value)[0] == "num"
                                                     and 0 <= _number(_strip(value)[1]) < (256 if conversion == "u8" else 32768))):
                self.narrow(reg, conversion)
        self.emit(f"call {function}", note or f"{function}(...)")

    def narrow(self, reg: str, conversion: str) -> None:
        """Widen a 32-bit int to a narrow C parameter type, as the calling convention requires."""
        ctype = _NARROW[conversion]
        if conversion == "u8":
            self.emit(f"andi {reg}, {reg}, 0xFF", f"{ctype} parameter: keep the low 8 bits")
        elif conversion == "u16":
            self.emit(f"slli {reg}, {reg}, 16", f"{ctype} parameter: keep the low 16 bits ...")
            self.emit(f"srli {reg}, {reg}, 16", "... with zeros above them")
        else:
            self.emit(f"slli {reg}, {reg}, 16", f"{ctype} parameter: keep the low 16 bits ...")
            self.emit(f"srai {reg}, {reg}, 16", "... with copies of their sign bit above")

    def colour(self, value: Any, fallback: str = "WHITE") -> tuple[str, str, None]:
        return ("imm", COLOR_MAP[color_name(value, fallback)], None)

    # ---- statements ------------------------------------------------------
    def block(self, statements: Any, slot: int) -> None:
        for stmt in statements if isinstance(statements, list) else []:
            if isinstance(stmt, dict):
                mark = self.trace.get(id(stmt)) if self.trace else None
                if mark:
                    self.lines.append(MAP_BEGIN + mark)
                self.statement(stmt, slot)
                if mark:
                    self.lines.append(MAP_END + mark)

    def heading(self, stmt: dict[str, Any]) -> None:
        """The comment above a block's instructions: the block in words and its C line."""
        sentence = narrate(describe_statement(stmt, self.show), self.c.python)
        c_lines = [line.strip() for line in self.c.statement(stmt, "", 0)]
        if stmt.get("op") in {"if", "if_button", "if_button_once", "if_touching"}:
            c_lines = c_lines[:1]
        elif stmt.get("op") == "repeat":
            c_lines = [line for line in c_lines if line.startswith("for (")][:1]
        lines = [sentence] if sentence else []
        lines += [f"|C:  {line}" for line in c_lines]
        if lines:
            self.lines.append("")
            self.lines.extend(comment_block(lines, "    "))

    def store(self, var: str, source: str = "a0") -> None:
        symbol = asm_symbol(var)
        self.emit(f"la t1, {symbol}", f"t1 = address of variable {var}")
        self.emit(f"sw {source}, 0(t1)", f"{var} = {source}")

    def statement(self, stmt: dict[str, Any], slot: int) -> None:
        op = stmt.get("op")
        var = state_name(stmt.get("var")) if "var" in stmt else ""
        lst = state_name(stmt.get("list")) if "list" in stmt else ""
        t = lambda key: self.tree(stmt.get(key))  # noqa: E731
        if op == "comment":
            self.lines.append("")
            self.lines.extend(comment_block([str(stmt.get("text"))], "    "))
            return
        self.heading(stmt)
        if op == "set":
            self.expr(t("value"), slot)
            self.store(var)
        elif op == "change":
            self.expr(t("delta"), slot)
            symbol = asm_symbol(var)
            self.emit(f"la t1, {symbol}", f"t1 = address of variable {var}")
            self.emit("lw t2, 0(t1)", f"t2 = {var}")
            self.emit("add t2, t2, a0", f"t2 = {var} + {self.show(stmt.get('delta'))[:30]}")
            self.emit("sw t2, 0(t1)", f"store the new {var}")
        elif op == "clamp":
            for bound, branch, reason in (("low", "bge", "not too small"), ("high", "ble", "not too large")):
                skip = self.new_label(f"clamp_{bound}")
                self.expr(t(bound), slot)
                symbol = asm_symbol(var)
                self.emit(f"la t1, {symbol}", f"t1 = address of variable {var}")
                self.emit("lw t2, 0(t1)", f"t2 = {var}")
                self.emit(f"{branch} t2, a0, {skip}", f"{var} {'>=' if bound == 'low' else '<='} {bound}: {reason}")
                self.expr(t(bound), slot)  # the C line names the bound again, so it is read again
                self.store(var)
                self.lines.append(asm_line(f"{skip}:"))
        elif op == "wrap":
            done, above = self.new_label("wrap_done"), self.new_label("wrap_test_high")
            self.expr(t("low"), slot)
            self.emit(f"la t1, {asm_symbol(var)}", f"t1 = address of variable {var}")
            self.emit("lw t2, 0(t1)", f"t2 = {var}")
            self.emit(f"bge t2, a0, {above}", f"{var} >= low: test the other end")
            self.wrap_size(stmt, slot)
            self.emit(f"la t1, {asm_symbol(var)}", "")
            self.emit("lw t2, 0(t1)", "")
            self.emit("add t2, t2, a0", f"{var} += size: re-enter from the top")
            self.emit("sw t2, 0(t1)", "")
            self.emit(f"j {done}", "")
            self.lines.append(asm_line(f"{above}:"))
            self.expr(t("high"), slot)
            self.emit(f"la t1, {asm_symbol(var)}", f"t1 = address of variable {var}")
            self.emit("lw t2, 0(t1)", f"t2 = {var}")
            self.emit(f"ble t2, a0, {done}", f"{var} <= high: inside the range")
            self.wrap_size(stmt, slot)
            self.emit(f"la t1, {asm_symbol(var)}", "")
            self.emit("lw t2, 0(t1)", "")
            self.emit("sub t2, t2, a0", f"{var} -= size: re-enter from the bottom")
            self.emit("sw t2, 0(t1)", "")
            self.lines.append(asm_line(f"{done}:"))
        elif op == "random":
            self.needs.add("random")
            self.call("prg32_kit_random", [("expr", t("low"), None), ("expr", t("high"), None)], slot,
                      "a0 = random number, low..high")
            self.store(var)
        elif op == "if":
            otherwise, end = self.new_label("else"), self.new_label("end_if")
            self.expr(t("cond"), slot)
            has_else = "else" in stmt
            self.emit(f"beqz a0, {otherwise if has_else else end}", "condition is 0 (false): skip the inner blocks")
            self.block(stmt.get("then"), slot)
            if has_else:
                self.emit(f"j {end}", "done: jump over the else blocks")
                self.lines.append(asm_line(f"{otherwise}:", "else: the condition was 0"))
                self.block(stmt.get("else"), slot)
            self.lines.append(asm_line(f"{end}:", f"end of if ({self.show(stmt.get('cond'))[:30]})"))
        elif op == "repeat":
            self.repeat(stmt, var, slot)
        elif op in {"list_fill", "list_set", "list_change"}:
            if lst not in self.lists:
                self.lines.extend(comment_block([f"unknown list {lst}"], "    "))
                return
            size = self.lists[lst]
            if op == "list_fill":
                self.needs.add("list_fill")
                self.call("prg32_kit_fill", [("addr", asm_symbol(lst), None), ("imm", size, None), ("expr", t("value"), None)],
                          slot, f"every item of {lst} = value")
            elif op == "list_set":
                self.needs.add("list_set")
                self.call("prg32_kit_set", [("addr", asm_symbol(lst), None), ("imm", size, None), ("expr", t("index"), None),
                                            ("expr", t("value"), None)], slot, f"{lst}[index] = value")
            else:
                self.needs.update({"list_set", "list_get"})
                value = ("bin", "+", ("index", lst, t("index")), t("delta"))
                self.call("prg32_kit_set", [("addr", asm_symbol(lst), None), ("imm", size, None), ("expr", t("index"), None),
                                            ("expr", value, None)], slot, f"{lst}[index] = old item + delta")
        elif op in {"if_button", "if_button_once"}:
            button = BUTTON_MAP.get(str(stmt.get("button") or "A").upper(), "PRG32_BTN_A")
            end = self.new_label("end_button")
            self.emit("lw a0, 0(sp)", "a0 = buttons held this frame (stack slot 0)")
            self.emit(f"li t0, {self.constant(button)}", f"t0 = the bit of {button}")
            self.emit("and a0, a0, t0", "keep only that bit")
            self.emit(f"beqz a0, {end}", "bit is 0: the button is up, skip")
            if op == "if_button_once":
                self.needs.add("once")
                self.emit("la t1, prg32_kit_previous_input", "")
                self.emit("lw t1, 0(t1)", "t1 = buttons held one frame ago")
                self.emit("and t1, t1, t0", "keep the same bit")
                self.emit(f"bnez t1, {end}", "it was already down: not a new press, skip")
            self.block(stmt.get("then"), slot)
            self.lines.append(asm_line(f"{end}:", "end of if button"))
        elif op == "if_touching":
            end = self.new_label("end_touching")
            args = [("expr", t(key), None) for key in ("ax", "ay", "aw", "ah", "bx", "by", "bw", "bh")]
            self.call("prg32_sprite_hitbox", args, slot, "a0 = 1 when the boxes overlap")
            self.emit(f"beqz a0, {end}", "no overlap: skip")
            self.block(stmt.get("then"), slot)
            self.lines.append(asm_line(f"{end}:", "end of if touching"))
        else:
            self.draw_statement(stmt, op, slot)

    def wrap_size(self, stmt: dict[str, Any], slot: int) -> None:
        """a0 = (high) - (low) + 1, the number of values in the wrap range."""
        size = ("bin", "+", ("bin", "-", ("paren", self.tree(stmt.get("high"))), ("paren", self.tree(stmt.get("low")))),
                ("num", "1"))
        self.expr(size, slot)

    def repeat(self, stmt: dict[str, Any], var: str, slot: int) -> None:
        step, last, count = slot, slot + 1, slot + 2
        test, end = self.new_label("repeat_test"), self.new_label("repeat_end")
        self.expr(self.tree(stmt.get("to")), slot + 3)
        self.emit(f"sw a0, {self.slot(last)}", f"last value, kept in stack slot {last}")
        self.emit(f"sw zero, {self.slot(count)}", f"rounds = 0 (stack slot {count})")
        self.expr(self.tree(stmt.get("from")), slot + 3)
        self.emit(f"sw a0, {self.slot(step)}", f"step = first value (stack slot {step})")
        self.lines.append(asm_line(f"{test}:", "test before every round"))
        self.emit(f"lw t0, {self.slot(step)}", "t0 = step")
        self.emit(f"lw t1, {self.slot(last)}", "t1 = last value")
        self.emit(f"bgt t0, t1, {end}", "step > last: the loop is over")
        self.emit(f"lw t2, {self.slot(count)}", "t2 = rounds done")
        self.emit(f"li t3, {MAX_REPEAT}", "")
        self.emit(f"bge t2, t3, {end}", f"{MAX_REPEAT} rounds done: stop (safety limit)")
        self.store(var, "t0")
        self.block(stmt.get("body"), slot + 3)
        self.emit(f"lw t0, {self.slot(step)}", "")
        self.emit("addi t0, t0, 1", "++step")
        self.emit(f"sw t0, {self.slot(step)}", "")
        self.emit(f"lw t0, {self.slot(count)}", "")
        self.emit("addi t0, t0, 1", "++rounds")
        self.emit(f"sw t0, {self.slot(count)}", "")
        self.emit(f"j {test}", "next round")
        self.lines.append(asm_line(f"{end}:", "end of repeat"))

    def draw_statement(self, stmt: dict[str, Any], op: Any, slot: int) -> None:
        t = lambda key: ("expr", self.tree(stmt.get(key)), None)  # noqa: E731
        typed = lambda key, conversion: ("expr", self.tree(stmt.get(key)), conversion)  # noqa: E731
        colour = self.colour
        if op == "clear":
            self.call("prg32_gfx_clear", [colour(stmt.get("color"), "BLACK")], slot, "paint the whole screen")
        elif op == "rect":
            self.call("prg32_gfx_rect", [t("x"), t("y"), t("w"), t("h"), colour(stmt.get("color"))], slot, "draw it")
        elif op == "rect_rgb":
            self.call("prg32_gfx_rect", [t("x"), t("y"), t("w"), t("h"), typed("rgb", "u16")], slot, "draw it")
        elif op == "frame":
            self.needs.add("frame_rect")
            self.call("prg32_kit_draw_frame", [t("x"), t("y"), t("w"), t("h"), colour(stmt.get("color"))], slot, "draw it")
        elif op == "pixel":
            self.call("prg32_gfx_pixel", [t("x"), t("y"), colour(stmt.get("color"))], slot, "draw it")
        elif op == "line":
            self.needs.add("line")
            self.call("prg32_kit_draw_line", [t("x1"), t("y1"), t("x2"), t("y2"), colour(stmt.get("color"))], slot, "draw it")
        elif op == "circle":
            self.needs.add("circle")
            self.call("prg32_kit_draw_circle", [t("x"), t("y"), t("r"), colour(stmt.get("color")),
                                                ("imm", 1 if stmt.get("fill") else 0, None)], slot, "draw it")
        elif op == "bitmap":
            self.needs.add("bitmap")
            self.call("prg32_kit_draw_bitmap", [t("x"), t("y"), ("addr", self.c.bitmap(stmt.get("rows")), None),
                                                colour(stmt.get("color")), t("scale")], slot, "draw it")
        elif op == "sprite8":
            self.call("prg32_sprite_draw_8x8", [t("x"), t("y"), ("addr", self.c.bitmap(stmt.get("rows")), None),
                                                colour(stmt.get("fg")), colour(stmt.get("bg"), "BLACK")], slot, "draw it")
        elif op == "palette_set":
            self.call("prg32_palette_set", [typed("index", "u8"), colour(stmt.get("color"))], slot, "set the entry")
        elif op == "clear_indexed":
            self.call("prg32_gfx_clear_indexed", [typed("index", "u8")], slot, "paint the whole screen")
        elif op == "rect_indexed":
            self.call("prg32_gfx_rect_indexed", [t("x"), t("y"), t("w"), t("h"), typed("index", "u8")], slot, "draw it")
        elif op == "pixel_indexed":
            self.call("prg32_gfx_pixel_indexed", [t("x"), t("y"), typed("index", "u8")], slot, "draw it")
        elif op == "text":
            self.call("prg32_gfx_text8", [t("x"), t("y"), ("addr", self.string(str(stmt.get("text") or "")), None),
                                          colour(stmt.get("fg")), colour(stmt.get("bg", "BLACK"), "BLACK")], slot, "draw the text")
        elif op == "number":
            self.needs.add("number")
            self.call("prg32_kit_draw_number", [t("x"), t("y"), t("value"), colour(stmt.get("fg")),
                                                colour(stmt.get("bg", "BLACK"), "BLACK")], slot, "draw the number")
        elif op == "tile_define":
            self.call("prg32_tile_define", [typed("id", "u8"), ("addr", self.c.bitmap(stmt.get("rows")), None),
                                            colour(stmt.get("fg")), colour(stmt.get("bg", "BLACK"), "BLACK")], slot, "store the tile")
        elif op == "playfield_clear":
            self.call("prg32_playfield_clear", [typed("layer", "u8"), typed("id", "u8")], slot, "fill the layer")
        elif op == "playfield_put":
            self.call("prg32_playfield_put", [typed("layer", "u8"), typed("tx", "u8"), typed("ty", "u8"), typed("id", "u8")],
                      slot, "place the tile")
        elif op == "playfield_scroll":
            self.call("prg32_playfield_scroll", [typed("layer", "u8"), t("x"), t("y")], slot, "scroll the layer")
        elif op == "playfield_camera":
            self.call("prg32_playfield_camera", [t("x"), t("y")], slot, "move the camera")
        elif op == "playfield_draw":
            self.call("prg32_playfield_draw", [typed("layer", "u8"), ("imm", 1 if stmt.get("transparent") else 0, None)],
                      slot, "draw the layer")
        elif op == "beep":
            self.needs.add("beep")
            note = ("kitcall", "prg32_kit_note_from_hz", [self.tree(stmt.get("freq"))], "the nearest note")
            self.call("prg32_audio_note", [("imm", 0, None), ("imm", 0, None), ("expr", note, "u8"), ("imm", 255, None),
                                           t("ms")], slot, "play it")
        elif op == "note":
            self.call("prg32_audio_note", [typed("channel", "u8"), ("imm", 0, None), typed("note", "u8"),
                                           ("imm", 255, None), t("ms")], slot, "play it")
        elif op == "note_off":
            self.call("prg32_audio_note_off", [typed("channel", "u8")], slot, "silence the channel")
        elif op == "volume":
            self.call("prg32_audio_set_master_volume", [typed("volume", "u8")], slot, "set the volume")
        elif op == "sfx":
            self.call("prg32_audio_note", [("imm", int(stmt["voice"]), None), ("imm", int(stmt["instrument"]), None),
                                           ("imm", int(stmt["note"]), None), ("imm", int(stmt["volume"]), None),
                                           ("imm", int(stmt["ms"]), None)], slot,
                      f"sound effect {stmt.get('name')}: instrument {stmt['instrument']} on voice {stmt['voice']}")
        elif op == "track_play":
            self.call("prg32_audio_play_track", [("imm", int(stmt["track"]), None)], slot,
                      f"soundtrack {stmt.get('name')}")
        elif op == "track_stop":
            self.call("prg32_audio_stop_track", [], slot, "stop the soundtrack")
        elif op == "sound_stop_all":
            self.call("prg32_audio_stop_all", [], slot, "silence every voice")
        elif op == "band":
            self.needs.add("band")
            has_value = "value" in stmt
            value = t("value") if has_value else ("imm", 0, None)
            self.call("prg32_kit_band", [("addr", self.string(str(stmt.get("text") or "")), None),
                                         ("imm", 1 if has_value else 0, None), value], slot, "update the band")
        elif op == "score_submit":
            self.call("prg32_score_submit_current_player", [("addr", self.string(self.title[:23]), None), t("value")],
                      slot, "record the score")
        elif op == "scoreboard":
            self.call("prg32_scoreboard_show", [("addr", self.string(self.title[:23]), None),
                                                ("addr", self.string(str(stmt.get("title") or "")), None)], slot, "show the table")
        elif op == "splash":
            self.call("prg32_splash_show_game", [("addr", self.string(str(stmt.get("title") or "")), None),
                                                 ("addr", self.string(str(stmt.get("subtitle") or "")), None), t("ms"),
                                                 colour(stmt.get("bg")), colour(stmt.get("fg")), colour(stmt.get("accent"))],
                      slot, "show the splash screen")
        elif op == "player_name":
            self.call("prg32_score_player_prompt", [], slot, "the firmware asks for a name")
        elif op == "mp_join":
            self.call("prg32_multiplayer_join", [("addr", self.string(self.title[:31]), None), ("imm", 1, None)], slot,
                      "join with PRG32_MP_FLAG_ENABLE (1)")
        elif op == "mp_share":
            self.call("prg32_multiplayer_set_local_state", [typed("x", "i16"), typed("y", "i16"), typed("look", "u16"),
                                                            ("imm", 0, None)], slot, "publish our state")
            self.call("prg32_multiplayer_tick", [], slot, "exchange messages with the relay")
        else:
            self.lines.extend(comment_block([f"Unsupported op: {op}"], "    "))

    # ---- functions -------------------------------------------------------
    def function(self, name: str, doc: list[str], statements: Any, reads_input: bool,
                 before: list[Any], after: list[Any]) -> list[str]:
        """One lifecycle function. ``before``/``after`` are (kind, ...) setup steps."""
        self.lines = []
        self.max_slot = 0
        base = 0
        if reads_input:
            self.lines.append("")
            self.lines.extend(comment_block(["Read every button once; the button blocks test bits of this value."], "    "))
            self.emit("call prg32_input_read", "a0 = one bit per button, 1 = held")
            self.emit(f"sw a0, {self.slot(0)}", "keep it in stack slot 0 (C: uint32_t input)")
            base = 1
        for step in before:
            self.setup(step, base)
        self.block(statements, base)
        for step in after:
            self.setup(step, base)
        body = self.lines
        frame = (4 * self.max_slot + 4 + 15) // 16 * 16
        slots = f"slots 0..{self.max_slot - 1} at 0(sp)..{4 * self.max_slot - 4}(sp), " if self.max_slot else ""
        out = [*doc,
               f"    .globl {name}",
               f"    .type {name}, @function",
               f"{name}:",
               *comment_block([f"Prologue. Stack frame of {frame} bytes: {slots}return address at {frame - 4}(sp)."],
                              "    "),
               asm_line(f"addi sp, sp, -{frame}", "make room for this function's stack frame"),
               asm_line(f"sw ra, {frame - 4}(sp)", "save the return address: calls below overwrite ra"),
               *body,
               "",
               *comment_block(["Epilogue: undo the prologue and return to the firmware."], "    "),
               asm_line(f"lw ra, {frame - 4}(sp)", "the firmware's return address"),
               asm_line(f"addi sp, sp, {frame}", "free the stack frame"),
               asm_line("ret", "back to the firmware"),
               f"    .size {name}, .-{name}",
               ""]
        return out

    def setup(self, step: tuple, slot: int) -> None:
        kind = step[0]
        if kind == "comment":
            self.lines.append("")
            self.lines.extend(comment_block([step[1]], "    "))
        elif kind == "var":
            _kind, name, initial = step
            self.emit(f"li t0, {initial}", f"t0 = {initial}")
            self.emit(f"la t1, {asm_symbol(name)}", f"t1 = address of variable {name}")
            self.emit("sw t0, 0(t1)", f"{name} = {initial}")
        elif kind == "list":
            _kind, name, size = step
            self.call("prg32_kit_fill", [("addr", asm_symbol(name), None), ("imm", size, None), ("imm", 0, None)], slot,
                      f"every item of {name} = 0")
        elif kind == "frame_reset":
            self.emit("la t0, prg32_kit_frame_count", "")
            self.emit("sw zero, 0(t0)", "no frames have finished yet")
        elif kind == "once_reset":
            self.emit("call prg32_input_read", "buttons already held do not count as a press")
            self.emit("la t0, prg32_kit_previous_input", "")
            self.emit("sw a0, 0(t0)", "previous input = buttons now")
        elif kind == "once_save":
            self.emit("lw t0, 0(sp)", "t0 = this frame's buttons")
            self.emit("la t1, prg32_kit_previous_input", "")
            self.emit("sw t0, 0(t1)", "remember them for the next frame")
        elif kind == "frame_count":
            self.emit("la t0, prg32_kit_frame_count", "")
            self.emit("lw t1, 0(t0)", "")
            self.emit("addi t1, t1, 1", "one more frame has finished")
            self.emit("sw t1, 0(t0)", "")


def _needed(needs: set[str]) -> list[str]:
    wanted = set(needs)
    for name in list(wanted):
        wanted |= _ASM_HELPER_NEEDS.get(name, set())
    return [name for name in _ASM_HELPER_ORDER if name in wanted]


def _section(title: str, *text: str) -> list[str]:
    rule = "=" * 74
    return ["/* " + rule, f" * {title}", *(f" * {line}".rstrip() for line in text), " * " + rule + " */"]


def _mnemonics(lines: list[str]) -> set[str]:
    found = set()
    for line in lines:
        if line.startswith("    ") and not line.startswith(("    /*", "     *", "    .")):
            word = line.split()[0]
            if word in ASM_GLOSSARY:
                found.add(word)
    return found


def ir_to_asm(game_ir: dict[str, Any], trace: dict[int, str] | None = None) -> str:
    """Write the game IR as RISC-V assembly, explained line by line for students.

    ``trace`` adds source-map marker lines, as in generator.ir_to_c.
    """
    prefix = c_identifier(game_ir.get("entry_prefix"), "prg32_game")
    emitter = _AsmEmitter(game_ir)
    state = [(state_name(item.get("name")), int32(c_int(item.get("initial"), 0)))
             for item in game_ir.get("state", []) if isinstance(item, dict)]
    phases = {phase: game_ir.get(phase, []) for phase in ("init", "update", "draw")}
    reads = {phase: _statements_read_input(statements) for phase, statements in phases.items()}
    # Which kit data the setup code needs is only known after the blocks are translated,
    # so the three functions are translated once to learn it, then again for real.
    for phase in phases:
        emitter.function("probe", [], phases[phase], reads[phase], [], [])
    probe_needs = set(emitter.needs) | ({"list_fill"} if emitter.lists else set())
    once, frame = "once" in probe_needs, "frame" in probe_needs
    emitter = _AsmEmitter(game_ir)
    emitter.trace = trace

    init_before: list[tuple] = []
    if state or emitter.lists:
        init_before.append(("comment", "Every variable and list gets its starting value again (also on a restart)."))
    init_before += [("var", name, initial) for name, initial in state]
    init_before += [("list", name, size) for name, size in emitter.lists.items()]
    if frame:
        init_before.append(("frame_reset",))
    if once:
        init_before.append(("once_reset",))
    if init_before and phases["init"]:
        init_before.append(("comment", "The lines of def init()." if emitter.c.python
                            else "The 'when game starts' blocks."))
    init = emitter.function(f"{prefix}_init", _doc(
        f"void {prefix}_init(void): runs once when the cartridge starts.",
        "Same work as the C function: starting values first, then " + (
            "the lines of def init()." if emitter.c.python else "the 'when game starts' blocks.")),
        phases["init"], reads["init"], init_before, [])
    update_after: list[tuple] = []
    if once:
        update_after.append(("once_save",))
    if frame:
        update_after.append(("frame_count",))
    if update_after:
        update_after.insert(0, ("comment", "End of frame bookkeeping."))
    update = emitter.function(f"{prefix}_update", _doc(
        f"void {prefix}_update(void): runs once per frame, before drawing (the game rules)."),
        phases["update"], reads["update"] or once, [], update_after)
    draw = emitter.function(f"{prefix}_draw", _doc(
        f"void {prefix}_draw(void): runs once per frame, after update (painting only)."),
        phases["draw"], reads["draw"], [], [])
    needs = emitter.needs | ({"list_fill"} if emitter.lists else set())
    helpers: list[str] = []
    for name in _needed(needs):
        helpers.extend(ASM_HELPERS[name])

    # ---- data ------------------------------------------------------------
    rodata: list[str] = []
    for text, label in emitter.strings.items():
        rodata.append(asm_line(f"{label}:", ""))
        rodata.append(asm_line(f'.asciz "{asm_string(text)}"', f"{len(text.encode('utf-8')) + 1} bytes with the final 0"))
    for rows, label in emitter.c.bitmaps.items():
        rodata.extend(["/* " + label + ":", *(f" *   {row}" for row in bitmap_picture(rows)), " */"])
        rodata.append(asm_line(f"{label}:", ""))
        rodata.append(asm_line(".byte " + ", ".join(f"0x{row:02X}" for row in rows), "8 rows, top to bottom"))
    wanted = set(_needed(needs))
    if "sin" in wanted:
        rodata.append(asm_line(".balign 2", "halfwords start at even addresses"))
        rodata.append(asm_line("prg32_kit_sine_table:", "round(256 * sin(d)) for d = 0..90"))
        for start in range(0, len(SINE_TABLE), 16):
            rodata.append(asm_line(".half " + ", ".join(str(v) for v in SINE_TABLE[start:start + 16]),
                                   f"degrees {start}..{min(start + 15, 90)}"))
    if "beep" in wanted:
        rodata.append(asm_line(".balign 2", ""))
        rodata.append(asm_line("prg32_kit_note_frequencies:", "Hz of MIDI notes 48 (C3) .. 84 (C6)"))
        for start in range(0, len(_NOTE_FREQUENCIES), 12):
            rodata.append(asm_line(".half " + ", ".join(str(v) for v in _NOTE_FREQUENCIES[start:start + 12]),
                                   f"notes {48 + start}..{min(48 + start + 11, 84)}"))

    data = [asm_line(".balign 4", "a word must start at a multiple of 4")]
    for name, initial in state:
        data.append(asm_line(f"{asm_symbol(name)}:", f"variable {name}" + (" (renamed: a register has that name)"
                                                                          if asm_symbol(name) != name else "")))
        data.append(asm_line(f".word {initial}", f"starts at {initial}"))
    bss = [asm_line(".balign 4", "")]
    for name, size in emitter.lists.items():
        bss.append(asm_line(f"{asm_symbol(name)}:", f"list {name}: {size} items of 4 bytes"))
        bss.append(asm_line(f".zero {4 * size}", f"{size} x 4 = {4 * size} bytes, all 0"))
    if frame:
        bss += [asm_line("prg32_kit_frame_count:", "frames finished since the start"), asm_line(".zero 4", "")]
    if once:
        bss += [asm_line("prg32_kit_previous_input:", "buttons held one frame ago"), asm_line(".zero 4", "")]
    if "band" in wanted:
        bss += [asm_line("prg32_kit_band_now:", "the band text wanted this frame"), asm_line(".zero 48", ""),
                asm_line("prg32_kit_band_shown:", "the band text on screen"), asm_line(".zero 48", "")]

    code_lines = [*helpers, *init, *update, *draw]
    used = sorted(_mnemonics(code_lines))
    constants = [name for name in CONSTANTS if name in emitter.constants]
    title = c_comment(emitter.title)
    lines = [
        "/*",
        f" * {title}",
        *((f" * RISC-V assembly generated by {GENERATED_BY} from the saved PRG32 Python.",
           " * Edit game.py, then regenerate this file. The C file made from the same game.py")
          if is_python(game_ir) else
          (f" * RISC-V assembly generated by {GENERATED_BY} from the saved Blocks JSON.",
           " * Edit the blocks, then regenerate this file. The C file made from the same blocks")),
        " * does exactly the same work: read the two side by side.",
        " *",
        " * HOW TO READ THIS FILE",
        f" * The firmware calls three functions: {prefix}_init once, then {prefix}_update and",
        f" * {prefix}_draw once per frame. Above the instructions of every block a comment says",
        " * what the block does and shows the C line it implements (C: ...); beside every",
        " * instruction a comment says what it does here. Parts: 1 constants, 2 read-only data,",
        " * 3 variables, 4 kit helper functions, 5 the three lifecycle functions.",
        " *",
        " * MACHINE AND CONVENTIONS",
        " * - RV32IM: 32-bit RISC-V with the M extension (mul, div, rem). Every register holds",
        " *   32 bits; an int is one 32-bit word (4 bytes) in memory, stored little-endian.",
        " * - RISC-V is a load/store machine: arithmetic works only on registers, so a variable",
        " *   is read with la (its address) + lw (load word), and written with la + sw.",
        " * - Calling convention (the RISC-V ABI ilp32): inputs in a0..a7, result in a0; ra holds",
        " *   the return address; t0..t6 and a0..a7 may be changed by any call; s0..s11 and sp",
        " *   must be given back unchanged; sp stays a multiple of 16.",
        " * - Expressions: the value of every expression ends up in a0. A simple operand goes",
        " *   into t0; the left side of a longer operation waits in a stack slot.",
        " * - Every address is computed from the pc (la, call), so the cartridge runs at",
        " *   whatever address the firmware loads it.",
        " * - This is a .S file: the build runs the C preprocessor first, so full-line comments",
        " *   are C block comments, and '#' starts a comment only after an instruction.",
        " *",
        " * DIRECTIVES (lines starting with a dot give orders to the assembler)",
        *(f" *   {line}" for line in DIRECTIVES),
        " *",
        " * INSTRUCTIONS USED IN THIS FILE (pseudo = the assembler writes a real instruction for it)",
        *(f" *   {ASM_GLOSSARY[word]}" for word in used),
        " */",
        asm_line(".option norelax", "keep every address pc-relative (no global pointer here)"),
        "",
        *_section("1. CONSTANTS", ".equ gives a name to a number, like #define in C."),
    ]
    for name in constants:
        value = CONSTANTS[name]
        words = rgb565_words(value) if name.startswith("PRG32_COLOR_") else (
            f"bit {value.bit_length() - 1} of the input value" if "_BTN_" in name else "")
        lines.append(asm_line(f".equ {name}, 0x{value:04X}" if name.startswith("PRG32_COLOR_") else f".equ {name}, {value}",
                              words))
    lines.append("")
    if rodata:
        lines += [*_section("2. READ-ONLY DATA", "Texts, pictures and tables. They never change."),
                  asm_line(".section .rodata", ""), *rodata, ""]
    lines += [*_section("3. VARIABLES",
                        ".data holds variables with a starting value; .bss holds lists and counters",
                        "that start at 0 (the firmware clears that memory, so the file stores no zeros).")]
    if state:
        lines += [asm_line(".section .data", ""), *data]
    if len(bss) > 1:
        lines += [asm_line(".section .bss", ""), *bss]
    lines.append("")
    lines += [asm_line(".section .text", "code"), asm_line(".balign 4", "instructions start at multiples of 4"), ""]
    if helpers:
        lines += [*_section("4. KIT HELPER FUNCTIONS",
                            "The same helpers as in the C file, written by hand in assembly. Only the",
                            "helpers this game uses are included."), *helpers]
    lines += [*_section("5. LIFECYCLE FUNCTIONS",
                        "Every function starts with a prologue (make a stack frame, save ra) and ends",
                        "with an epilogue (restore ra, free the frame, ret).",
                        "Stack slot n is the word at 4*n(sp); slot 0 holds the buttons when a function",
                        "reads them."), *init, *update, *draw]
    return "\n".join(lines).rstrip() + "\n"


def blocks_to_asm(blocks_json: Any, project: dict[str, Any] | None = None) -> tuple[dict[str, Any], str]:
    game_ir = blocks_to_ir(blocks_json, project)
    return game_ir, ir_to_asm(game_ir)
