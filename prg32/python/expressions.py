"""The small integer expression language used inside block text fields.

Students type expressions such as ``ball_x + 4``, ``bricks[i]`` or
``cos(angle) * 3 / 256`` into block fields. This module checks that text and
turns it into two forms:

* a *normalized* form saved in the game IR (the same words the student typed,
  with tidy spacing), which the browser simulator evaluates; and
* a *C* form, where list reads and kit functions become calls to small,
  visible helper functions in the generated C file.

Nothing outside this little grammar can reach the generated C: no semicolons,
braces, strings, comments, preprocessor lines, assignments or unknown calls.
``static/js/simulator.js`` implements the same grammar for the browser.
"""

# Shared with PRG32-Construction-Kit: prg32_construction_kit/expressions.py at revision 0b9f1c0.
# Keep the two copies identical apart from the lines marked "SDK:"; see
# docs/software/python_language.md#keeping-the-sdk-and-the-kit-aligned.

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

# One token: a whole number, a name, or an allowed operator/punctuation mark.
_TOKEN_RE = re.compile(
    r"\s*(?:(?P<number>(?:0[xX][0-9A-Fa-f]+|[0-9]+)(?![A-Za-z0-9_]))"
    r"|(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"|(?P<op><<|>>|<=|>=|==|!=|&&|\|\||[-+*/%()<>!&|^~\[\],]))"
)
_IDENTIFIER_RE = re.compile(r"[^A-Za-z0-9_]+")
MAX_INT = 0x7FFFFFFF

# Names students cannot use for variables because C already uses them.
# `input` is the local button mask inside the generated update function.
RESERVED_NAMES = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
    "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while", "bool", "true", "false", "asm", "input",
    # Type names from <stdint.h> and <stddef.h> that the generated C uses or includes.
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "intptr_t", "uintptr_t", "size_t", "main",
}

# Kit functions a student may call: name -> (number of arguments, C helper).
FUNCTIONS = {
    "abs": (1, "prg32_kit_abs"),
    "sign": (1, "prg32_kit_sign"),
    "min": (2, "prg32_kit_min"),
    "max": (2, "prg32_kit_max"),
    "clamp": (3, "prg32_kit_clamp"),
    "sin": (1, "prg32_kit_sin"),
    "cos": (1, "prg32_kit_cos"),
    "rgb": (3, "prg32_kit_rgb"),
    "random": (2, "prg32_kit_random"),
    "frame": (0, "prg32_kit_frame"),
    "tile": (3, "prg32_kit_tile"),
    "peers": (0, "prg32_kit_peers"),
    "peer_x": (1, "prg32_kit_peer_x"),
    "peer_y": (1, "prg32_kit_peer_y"),
    "peer_look": (1, "prg32_kit_peer_look"),
}
# Functions that need an optional PRG32 service (a cartridge feature bit).
FUNCTION_FEATURES = {"tile": "tilemap", "peers": "multiplayer", "peer_x": "multiplayer",
                     "peer_y": "multiplayer", "peer_look": "multiplayer"}

# Named PRG32 constants an expression may use, with their values.
CONSTANTS: dict[str, int] = {
    "PRG32_GAME_W": 320, "PRG32_GAME_H": 200, "PRG32_SCREEN_W": 320, "PRG32_SCREEN_H": 200,
    "PRG32_LCD_W": 320, "PRG32_LCD_H": 240, "PRG32_TEXT_COLS": 40, "PRG32_TEXT_ROWS": 25,
    "PRG32_TILE_W": 8, "PRG32_TILE_H": 8, "PRG32_TILE_COLS": 40, "PRG32_TILE_ROWS": 25,
    "PRG32_PLAYFIELD_LAYERS": 2, "PRG32_PLAYFIELD_COLS": 64, "PRG32_PLAYFIELD_ROWS": 32,
    "PRG32_BTN_LEFT": 1 << 0, "PRG32_BTN_RIGHT": 1 << 1, "PRG32_BTN_UP": 1 << 2, "PRG32_BTN_DOWN": 1 << 3,
    "PRG32_BTN_A": 1 << 4, "PRG32_BTN_B": 1 << 5, "PRG32_BTN_START": 1 << 6, "PRG32_BTN_SELECT": 1 << 6,
    "PRG32_P2_BTN_LEFT": 1 << 8, "PRG32_P2_BTN_RIGHT": 1 << 9, "PRG32_P2_BTN_UP": 1 << 10,
    "PRG32_P2_BTN_DOWN": 1 << 11, "PRG32_P2_BTN_A": 1 << 12, "PRG32_P2_BTN_B": 1 << 13,
    "PRG32_P2_BTN_START": 1 << 14, "PRG32_P2_BTN_SELECT": 1 << 14,
    "PRG32_COLOR_BLACK": 0x0000, "PRG32_COLOR_WHITE": 0xFFFF, "PRG32_COLOR_RED": 0xF800,
    "PRG32_COLOR_GREEN": 0x07E0, "PRG32_COLOR_BLUE": 0x001F, "PRG32_COLOR_YELLOW": 0xFFE0,
    "PRG32_COLOR_CYAN": 0x07FF, "PRG32_COLOR_MAGENTA": 0xF81F, "PRG32_COLOR_ORANGE": 0xFD20,
    "PRG32_COLOR_PINK": 0xFE19, "PRG32_COLOR_GRAY": 0x8410, "PRG32_COLOR_DARK_GRAY": 0x4208,
    "PRG32_COLOR_BROWN": 0xA145, "PRG32_COLOR_PURPLE": 0x780F, "PRG32_COLOR_NAVY": 0x000F,
    "PRG32_COLOR_LIME": 0x87E0,
}

# C operator precedence, loosest first (the same table is in simulator.js).
_BINARY_LEVELS = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="], ["<", ">", "<=", ">="],
                  ["<<", ">>"], ["+", "-"], ["*", "/", "%"]]


def c_identifier(value: str | None, fallback: str = "game") -> str:
    raw = (value or fallback).strip().lower()
    raw = _IDENTIFIER_RE.sub("_", raw).strip("_")
    if not raw:
        raw = fallback
    if raw[0].isdigit():
        raw = f"_{raw}"
    return raw[:48]


def state_name(value: Any) -> str:
    """Return the C name for a student variable or list."""
    name = c_identifier(str(value) if value is not None else None, "state")
    if name in RESERVED_NAMES or name.startswith("prg32_"):
        name = f"{name}_var"
    return name


class ExpressionError(ValueError):
    pass


@dataclass
class Expression:
    """A checked expression: its tree, and the variables and features it uses."""

    tree: tuple
    names: set[str] = field(default_factory=set)
    features: set[str] = field(default_factory=set)


def _tokenize(text: str) -> list[tuple[str, str]]:
    tokens: list[tuple[str, str]] = []
    pos = 0
    while pos < len(text):
        if not text[pos:].strip():
            break
        match = _TOKEN_RE.match(text, pos)
        if not match or match.end() == pos:
            raise ExpressionError("unexpected character")
        kind = match.lastgroup
        tokens.append((kind, match.group(kind)))
        pos = match.end()
    return tokens


class _Parser:
    def __init__(self, tokens: list[tuple[str, str]], lists: dict[str, int]):
        self.tokens = tokens
        self.pos = 0
        self.lists = lists
        self.names: set[str] = set()
        self.features: set[str] = set()

    def peek_op(self) -> str | None:
        if self.pos < len(self.tokens) and self.tokens[self.pos][0] == "op":
            return self.tokens[self.pos][1]
        return None

    def expect(self, op: str) -> None:
        if self.peek_op() != op:
            raise ExpressionError(f"expected {op}")
        self.pos += 1

    def parse(self) -> tuple:
        tree = self.binary(0)
        if self.pos != len(self.tokens):
            raise ExpressionError("extra text")
        return tree

    def binary(self, level: int) -> tuple:
        if level >= len(_BINARY_LEVELS):
            return self.unary()
        left = self.binary(level + 1)
        while self.peek_op() in _BINARY_LEVELS[level]:
            op = self.tokens[self.pos][1]
            self.pos += 1
            left = ("bin", op, left, self.binary(level + 1))
        return left

    def unary(self) -> tuple:
        op = self.peek_op()
        if op in {"-", "+", "!", "~"}:
            self.pos += 1
            return ("unary", op, self.unary())
        return self.primary()

    def primary(self) -> tuple:
        if self.pos >= len(self.tokens):
            raise ExpressionError("missing value")
        kind, text = self.tokens[self.pos]
        self.pos += 1
        if kind == "number":
            value = int(text, 16) if text[:2].lower() == "0x" else int(text, 10)
            if value > MAX_INT:
                raise ExpressionError("number too large")
            return ("num", text)
        if kind == "name":
            return self.name(text)
        if text == "(":
            inner = self.binary(0)
            self.expect(")")
            return ("paren", inner)
        raise ExpressionError(f"unexpected {text}")

    def name(self, text: str) -> tuple:
        after = self.peek_op()
        if after == "(":
            fname = text.lower()
            if fname not in FUNCTIONS:
                raise ExpressionError(f"unknown function {text}")
            self.pos += 1
            args: list[tuple] = []
            if self.peek_op() != ")":
                args.append(self.binary(0))
                while self.peek_op() == ",":
                    self.pos += 1
                    args.append(self.binary(0))
            self.expect(")")
            if len(args) != FUNCTIONS[fname][0]:
                raise ExpressionError(f"{fname} needs {FUNCTIONS[fname][0]} values")
            if fname in FUNCTION_FEATURES:
                self.features.add(FUNCTION_FEATURES[fname])
            return ("call", fname, args)
        if text.startswith("PRG32_"):
            if text not in CONSTANTS:
                raise ExpressionError(f"unknown constant {text}")
            return ("const", text)
        name = state_name(text)
        if after == "[":
            if name not in self.lists:
                raise ExpressionError(f"{name} is not a list")
            self.pos += 1
            index = self.binary(0)
            self.expect("]")
            return ("index", name, index)
        if name in self.lists:
            raise ExpressionError(f"{name} is a list; use {name}[position]")
        self.names.add(name)
        return ("name", name)


def parse_expr(value: Any, lists: dict[str, int] | None = None) -> Expression:
    """Parse one block field. Raises ExpressionError when the text is not allowed."""
    text = str(value).strip()
    if not text or len(text) > 200 or "/*" in text or "*/" in text or "//" in text:
        raise ExpressionError("empty, too long, or contains a comment")
    parser = _Parser(_tokenize(text), lists or {})
    tree = parser.parse()
    return Expression(tree, parser.names, parser.features)


def _is_nonzero_number(tree: tuple) -> bool:
    """True for a plain number like 2 or 0x10 (dividing by it is always safe)."""
    if tree[0] != "num":
        return False
    text = tree[1]
    return (int(text, 16) if text[:2].lower() == "0x" else int(text, 10)) != 0


def render(tree: tuple, c_lists: dict[str, int] | None = None, needs: set[str] | None = None) -> str:
    """Write a tree back as text.

    Without ``c_lists`` this is the normalized kit form. With ``c_lists`` (list
    name -> size) it is C: list reads and functions become kit helper calls,
    and the helper names are added to ``needs``.
    """
    kind = tree[0]
    to_c = c_lists is not None
    if kind in {"num", "name", "const"}:
        return tree[1]
    if kind == "paren":
        return "(" + render(tree[1], c_lists, needs) + ")"
    if kind == "unary":
        inner = render(tree[2], c_lists, needs)
        space = " " if inner[:1] in {"-", "+"} else ""
        return f"{tree[1]}{space}{inner}"
    if kind == "bin":
        left, right = render(tree[2], c_lists, needs), render(tree[3], c_lists, needs)
        if to_c and tree[1] in {"/", "%"} and not _is_nonzero_number(tree[3]):
            # In C, x / 0 is undefined. The helper gives the RISC-V answer
            # (x / 0 is -1, x % 0 is x), the same as the browser simulator.
            helper = "div" if tree[1] == "/" else "mod"
            if needs is not None:
                needs.add(helper)
            return f"prg32_kit_{helper}({left}, {right})"
        return f"{left} {tree[1]} {right}"
    if kind == "index":
        index = render(tree[2], c_lists, needs)
        if not to_c:
            return f"{tree[1]}[{index}]"
        if needs is not None:
            needs.add("list_get")
        return f"prg32_kit_get({tree[1]}, {c_lists[tree[1]]}, {index})"
    if kind == "call":
        args = ", ".join(render(arg, c_lists, needs) for arg in tree[2])
        if not to_c:
            return f"{tree[1]}({args})"
        helper = FUNCTIONS[tree[1]][1]
        if needs is not None:
            needs.add(tree[1])
        return f"{helper}({args})"
    raise ExpressionError(f"unknown expression part {kind}")


def check_expr(value: Any, lists: dict[str, int] | None = None) -> str | None:
    """Return the normalized expression text, or None when the text is not allowed."""
    try:
        return render(parse_expr(value, lists).tree)
    except (ExpressionError, RecursionError):
        return None


def expr_to_c(text: str, lists: dict[str, int], needs: set[str]) -> str:
    """Translate a normalized IR expression to C (it was already checked)."""
    try:
        return render(parse_expr(text, lists).tree, lists, needs)
    except (ExpressionError, RecursionError):
        return "0"
