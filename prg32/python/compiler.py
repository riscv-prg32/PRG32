"""PRG32 Python: a small, typed Python subset compiled ahead of time (ADR-0026).

A Python project is written in PRG32 Python and becomes a PRG32 cartridge without
ever running Python on the server:

    saved game.py
      -> ast.parse with size and nesting limits (the program is never imported or run)
      -> the allow-list and type checks below (PRG32 Python, language version 1)
      -> the kit's game IR, the same one the Blocks use (generator.py)
      -> commented C (generator.ir_to_c) and, on request, RISC-V assembly (asm_generator)
      -> the sandboxed PRG32 toolchain -> PRG32 cartridge

The language is described for students and teachers in docs/software/python_language.md
(in the kit: docs/python_language.md).
Its rules in short:

* ``import prg32`` and three functions ``init``, ``update`` and ``draw``.
* Variables have a type: ``score: int = 0``, ``alive: bool = True``,
  ``bricks: list[int] = [0] * 10``. ``int`` is a signed 32-bit whole number.
* ``if``/``elif``/``else``, ``for name in range(...)`` (at most 1024 rounds),
  assignments, ``+=`` and friends, helper functions (they are copied into the
  place they are called: no recursion), and the ``prg32`` module's functions.
* Anything else is refused with a message that names the line and column.

Every IR statement remembers the Python lines it came from, so the debugger can
show Python next to the generated C and assembly (source_maps.py).
"""

# Shared with PRG32-Construction-Kit: prg32_construction_kit/python_project.py at revision 0b9f1c0.
# Keep the two copies identical apart from the lines marked "SDK:"; see
# docs/software/python_language.md#keeping-the-sdk-and-the-kit-aligned.

from __future__ import annotations

import ast
import sys
from dataclasses import dataclass, field
from typing import Any

from .expressions import CONSTANTS, FUNCTION_FEATURES, FUNCTIONS, MAX_INT, ExpressionError, c_identifier, \
    parse_expr, render, state_name
from .generator import BUTTON_MAP, BUTTON_NAMES, COLOR_NAMES, IR_ABI, MAX_LIST_SIZE, MAX_REPEAT, _ON_PIXEL
from .messages import fields as message_fields, text as say

LANGUAGE_VERSION = 1
LANGUAGE = f"prg32-python-{LANGUAGE_VERSION}"
# The Python grammar the parser uses. The allow-list below decides what PRG32 Python accepts,
# so a newer CPython can only make the parser stricter, never let more through.
GRAMMAR = f"cpython-ast-{sys.version_info.major}.{sys.version_info.minor}"
SOURCE_FILE = "game.py"

# Limits. A classroom game is far below all of them; they stop a huge or deeply nested
# file from using the server's memory or time.
MAX_SOURCE_BYTES = 64 * 1024
MAX_LINES = 4000
MAX_NODES = 20000
MAX_BRACKET_DEPTH = 32
MAX_AST_DEPTH = 64
MAX_BLOCK_DEPTH = 12
MAX_FUNCTIONS = 64
MAX_SYMBOLS = 256
MAX_INLINE_DEPTH = 8
MAX_IR_STATEMENTS = 4000
MAX_LIST_LITERAL = 64
MAX_TEXT = 64

LIFECYCLE = ("init", "update", "draw")
TYPES = ("int", "bool")
# Python names a student may not define: they mean something else in Python or in PRG32 Python.
_BUILTIN_NAMES = {"abs", "min", "max", "range", "print", "len", "int", "bool", "list", "str", "float", "input",
                  "True", "False", "None", "prg32", "global", "pass"}

# What a C-only name reserved by the generated code becomes (state_name adds "_var"):
# such names are refused, so a variable is called the same in game.py, game.c and game.S.


class PythonError(ValueError):
    """A problem in a PRG32 Python program, with a stable code and the place in game.py.

    ``str(error)`` is "Line N: message", the form the IDE already shows as a problem.
    """

    def __init__(self, code: str, message: str, node: ast.AST | None = None, *, line: int = 0, column: int = 0,
                 end_line: int = 0, end_column: int = 0):
        if node is not None:
            line = getattr(node, "lineno", 0) or 0
            column = (getattr(node, "col_offset", 0) or 0) + 1
            end_line = getattr(node, "end_lineno", line) or line
            end_column = (getattr(node, "end_col_offset", column) or column) + 1
        self.code = code
        self.message = message
        self.line, self.column, self.end_line, self.end_column = line, column, end_line or line, end_column or column
        super().__init__(f"Line {line}: {message}" if line else message)

    def as_problem(self) -> dict[str, Any]:
        return {"code": self.code, "severity": "error", "message": self.message, "line": self.line,
                "column": self.column, "end_line": self.end_line, "end_column": self.end_column,
                **message_fields(self.message)}


# ---------------------------------------------------------------------------
# The prg32 module: what a PRG32 Python program may call
# ---------------------------------------------------------------------------
# Statement functions: name -> (IR op, parameters, feature, help). A parameter is
# (python name, IR key, kind, default); a default of None means "must be given".
# Kinds: "int" a whole-number expression; "color" a fixed prg32 colour such as prg32.RED;
# "text" a short text in quotes; "rows" eight picture rows such as ["..XX....", ...];
# "flag" True or False; "effect"/"track" the name of an AudioStudio sound effect/soundtrack.
_P = tuple[str, str, str, Any]
STATEMENTS: dict[str, tuple[str, list[_P], str | None, str]] = {
    "clear": ("clear", [("color", "color", "color", "BLACK")], None,
              "Fill the whole screen with one colour."),
    "rect": ("rect", [("x", "x", "int", None), ("y", "y", "int", None), ("w", "w", "int", None),
                      ("h", "h", "int", None), ("color", "color", "color", "WHITE")], None,
             "Draw a filled rectangle: top-left corner x, y, width w, height h."),
    "frame": ("frame", [("x", "x", "int", None), ("y", "y", "int", None), ("w", "w", "int", None),
                        ("h", "h", "int", None), ("color", "color", "color", "WHITE")], None,
              "Draw the outline of a rectangle."),
    "rect_rgb": ("rect_rgb", [("x", "x", "int", None), ("y", "y", "int", None), ("w", "w", "int", None),
                              ("h", "h", "int", None), ("rgb", "rgb", "int", None)], None,
                 "Draw a filled rectangle in any RGB565 colour, for example prg32.rgb(255, 128, 0)."),
    "pixel": ("pixel", [("x", "x", "int", None), ("y", "y", "int", None), ("color", "color", "color", "WHITE")], None,
              "Light one pixel."),
    "line": ("line", [("x1", "x1", "int", None), ("y1", "y1", "int", None), ("x2", "x2", "int", None),
                      ("y2", "y2", "int", None), ("color", "color", "color", "WHITE")], None,
             "Draw a straight line from x1, y1 to x2, y2."),
    "circle": ("circle", [("x", "x", "int", None), ("y", "y", "int", None), ("r", "r", "int", None),
                          ("color", "color", "color", "WHITE"), ("filled", "fill", "flag", False)], None,
               "Draw a circle around x, y with radius r (filled=True fills it)."),
    "bitmap": ("bitmap", [("rows", "rows", "rows", None), ("x", "x", "int", None), ("y", "y", "int", None),
                          ("color", "color", "color", "WHITE"), ("scale", "scale", "int", 1)], None,
               "Draw an 8x8 picture: eight rows of eight characters, '.' is empty."),
    "sprite8": ("sprite8", [("rows", "rows", "rows", None), ("x", "x", "int", None), ("y", "y", "int", None),
                            ("fg", "fg", "color", "WHITE"), ("bg", "bg", "color", "BLACK")], "sprites",
                "Draw an 8x8 sprite with the firmware's sprite service."),
    "pixel_indexed": ("pixel_indexed", [("x", "x", "int", None), ("y", "y", "int", None),
                                        ("index", "index", "int", None)], None,
                      "Light one pixel with a palette colour number."),
    "rect_indexed": ("rect_indexed", [("x", "x", "int", None), ("y", "y", "int", None), ("w", "w", "int", None),
                                      ("h", "h", "int", None), ("index", "index", "int", None)], None,
                     "Draw a filled rectangle with a palette colour number."),
    "palette_set": ("palette_set", [("index", "index", "int", None), ("color", "color", "color", None)], None,
                    "Change one palette colour."),
    "clear_indexed": ("clear_indexed", [("index", "index", "int", None)], None,
                      "Fill the screen with a palette colour number."),
    "text": ("text", [("x", "x", "int", None), ("y", "y", "int", None), ("text", "text", "text", None),
                      ("fg", "fg", "color", "WHITE"), ("bg", "bg", "color", "BLACK")], None,
             "Write a short text with the 8x8 font."),
    "number": ("number", [("value", "value", "int", None), ("x", "x", "int", None), ("y", "y", "int", None),
                          ("fg", "fg", "color", "WHITE"), ("bg", "bg", "color", "BLACK")], None,
               "Write a whole number with the 8x8 font."),
    "tile_define": ("tile_define", [("id", "id", "int", None), ("rows", "rows", "rows", None),
                                    ("fg", "fg", "color", "WHITE"), ("bg", "bg", "color", "BLACK")], "tilemap",
                    "Define tile number id from an 8x8 picture."),
    "playfield_clear": ("playfield_clear", [("layer", "layer", "int", 0), ("id", "id", "int", 0)], "tilemap",
                        "Fill a playfield layer with one tile."),
    "playfield_put": ("playfield_put", [("layer", "layer", "int", None), ("tx", "tx", "int", None),
                                        ("ty", "ty", "int", None), ("id", "id", "int", None)], "tilemap",
                      "Put tile id at column tx, row ty of a playfield layer."),
    "playfield_scroll": ("playfield_scroll", [("layer", "layer", "int", None), ("x", "x", "int", None),
                                              ("y", "y", "int", None)], "tilemap",
                         "Scroll a playfield layer to x, y."),
    "playfield_camera": ("playfield_camera", [("x", "x", "int", None), ("y", "y", "int", None)], "tilemap",
                         "Move the playfield camera to x, y."),
    "playfield_draw": ("playfield_draw", [("layer", "layer", "int", 0),
                                          ("transparent", "transparent", "flag", False)], "tilemap",
                       "Draw a playfield layer (transparent=True leaves tile 0 see-through)."),
    "beep": ("beep", [("freq", "freq", "int", None), ("ms", "ms", "int", None)], "audio",
             "Play the note nearest to freq Hz for ms milliseconds."),
    "note": ("note", [("note", "note", "int", None), ("voice", "channel", "int", 0), ("ms", "ms", "int", 250)],
             "audio", "Play MIDI note number note (60 is middle C) on voice 0-7."),
    "note_off": ("note_off", [("voice", "channel", "int", 0)], "audio", "Release the note on voice 0-7."),
    "volume": ("volume", [("volume", "volume", "int", None)], "audio", "Set the master volume, 0-255."),
    "sound_effect": ("sfx", [("name", "name", "effect", None)], "audio",
                     "Play a sound effect made in AudioStudio, by its name."),
    "soundtrack_play": ("track_play", [("name", "name", "track", None)], "audio",
                        "Start a soundtrack made in AudioStudio, by its name."),
    "soundtrack_stop": ("track_stop", [], "audio", "Stop the soundtrack."),
    "sound_stop_all": ("sound_stop_all", [], "audio", "Stop every sound."),
    "band": ("band", [("text", "text", "text", None), ("value", "value", "int", "")], None,
             "Show a short text (and a number) in the band above the game."),
    "score_submit": ("score_submit", [("value", "value", "int", None)], None,
                     "Send the player's score to the scoreboard."),
    "scoreboard": ("scoreboard", [("title", "title", "text", "HIGH SCORES")], None, "Show the scoreboard."),
    "splash": ("splash", [("title", "title", "text", None), ("subtitle", "subtitle", "text", "PRESS START"),
                          ("ms", "ms", "int", 1500), ("bg", "bg", "color", "BLACK"), ("fg", "fg", "color", "WHITE"),
                          ("accent", "accent", "color", "YELLOW")], None,
               "Show the title screen for ms milliseconds."),
    "player_name": ("player_name", [], None, "Ask the player for their name."),
    "mp_join": ("mp_join", [], "multiplayer", "Join a multiplayer game."),
    "mp_share": ("mp_share", [("x", "x", "int", None), ("y", "y", "int", None), ("look", "look", "int", 0)],
                 "multiplayer", "Share this player's position and look with the others."),
}
_TEXT_LIMITS = {"band": 24, "scoreboard": 32, "splash": 32}

# Conditions: only as the whole test of an ``if`` (or joined with ``and``), never with else/elif.
CONDITIONS: dict[str, tuple[str, list[_P], str]] = {
    "held": ("if_button", [("button", "button", "button", None)], "True while the button is held down."),
    "pressed": ("if_button_once", [("button", "button", "button", None)],
                "True only in the frame the button goes down."),
    "touching": ("if_touching", [(name, name, "int", None) for name in ("ax", "ay", "aw", "ah", "bx", "by", "bw", "bh")],
                 "True when rectangle a (ax, ay, aw, ah) overlaps rectangle b."),
}

# Value functions: prg32.<name>(...) -> the kit expression function of the same name.
VALUE_FUNCTIONS = {name: arity for name, (arity, _helper) in FUNCTIONS.items()}
VALUE_HELP = {
    "abs": "The number without its sign.", "sign": "-1, 0 or 1.", "min": "The smaller of two numbers.",
    "max": "The larger of two numbers.", "clamp": "clamp(value, low, high): value kept between low and high.",
    "sin": "sin(degrees) * 256, a whole number.", "cos": "cos(degrees) * 256, a whole number.",
    "rgb": "rgb(red, green, blue) with 0-255 each: an RGB565 colour.",
    "random": "random(low, high): a whole number from low to high, both included.",
    "frame": "The number of frames since the game started.", "tile": "tile(layer, tx, ty): the tile there.",
    "peers": "How many other players are connected.", "peer_x": "peer_x(n): x of player n.",
    "peer_y": "peer_y(n): y of player n.", "peer_look": "peer_look(n): the look of player n.",
}
_IMPURE = {"random", "frame", "peers", "peer_x", "peer_y", "peer_look", "tile"}
BUILTIN_VALUES = {"abs": 1, "min": 2, "max": 2}

# prg32.<NAME> constants: every PRG32_<NAME> the expression language knows, plus colour and
# button short names (prg32.RED is PRG32_COLOR_RED, prg32.LEFT is PRG32_BTN_LEFT).
CONSTANT_NAMES: dict[str, str] = {name[len("PRG32_"):]: name for name in CONSTANTS}
CONSTANT_NAMES.update({color: f"PRG32_COLOR_{color}" for color in COLOR_NAMES})
CONSTANT_NAMES.update({button: f"PRG32_BTN_{button}" for button in BUTTON_NAMES})


def api_reference() -> dict[str, Any]:
    """The prg32 module for the IDE's completion and hover (tools/build_language_index.py) and the docs."""
    def signature(name: str, params: list[_P]) -> str:
        parts = [p[0] if p[3] is None else f"{p[0]}={_default_text(p)}" for p in params]
        return f"prg32.{name}({', '.join(parts)})"

    functions = [{"name": name, "kind": "statement", "signature": signature(name, params), "help": help_text,
                  "params": [p[0] for p in params]}
                 for name, (_op, params, _feature, help_text) in sorted(STATEMENTS.items())]
    functions += [{"name": name, "kind": "condition", "signature": signature(name, params), "help": help_text,
                   "params": [p[0] for p in params]}
                  for name, (_op, params, help_text) in sorted(CONDITIONS.items())]
    functions += [{"name": name, "kind": "value", "help": VALUE_HELP.get(name, ""),
                   "signature": f"prg32.{name}({', '.join('abcdefgh'[:arity])})", "params": list("abcdefgh"[:arity])}
                  for name, arity in sorted(VALUE_FUNCTIONS.items())]
    return {"language": LANGUAGE, "version": LANGUAGE_VERSION, "module": "prg32", "functions": functions,
            "constants": sorted(CONSTANT_NAMES), "colors": list(COLOR_NAMES), "buttons": list(BUTTON_NAMES),
            "builtins": sorted(BUILTIN_VALUES), "types": list(TYPES), "lifecycle": list(LIFECYCLE),
            "limits": {"source_bytes": MAX_SOURCE_BYTES, "lines": MAX_LINES, "repeat": MAX_REPEAT,
                       "list_size": MAX_LIST_SIZE, "text": MAX_TEXT, "inline_depth": MAX_INLINE_DEPTH}}


def _default_text(param: _P) -> str:
    default = param[3]
    if param[2] == "color":
        return f"prg32.{default}"
    if param[2] == "text":
        return f'"{default}"'
    if default == "":
        return "None"
    return repr(default)


# ---------------------------------------------------------------------------
# Typed expressions
# ---------------------------------------------------------------------------
# C precedence of the kit expression language, loosest first (expressions._BINARY_LEVELS).
_PREC = {"||": 0, "&&": 1, "|": 2, "^": 3, "&": 4, "==": 5, "!=": 5, "<": 6, ">": 6, "<=": 6, ">=": 6,
         "<<": 7, ">>": 7, "+": 8, "-": 8, "*": 9, "/": 9, "%": 9}
_UNARY_PREC = 10
_ATOM = 11
_BINOPS = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.FloorDiv: "/", ast.Mod: "%", ast.LShift: "<<",
           ast.RShift: ">>", ast.BitAnd: "&", ast.BitOr: "|", ast.BitXor: "^"}
_COMPARE = {ast.Eq: "==", ast.NotEq: "!=", ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">="}
_REFUSED_BINOPS = {
    ast.Div: "'/' makes a fraction in Python; PRG32 numbers are whole: use '//'",
    ast.Pow: "'**' is not part of PRG32 Python yet: multiply, or use a constant",
    ast.MatMult: "'@' is not part of PRG32 Python",
}


@dataclass
class Expr:
    text: str          # in the kit expression language (C syntax)
    type: str          # "int" or "bool"
    prec: int          # how tightly the text binds (for brackets)
    impure: bool = False  # calls random()/frame()...: may not be copied


@dataclass
class Symbol:
    c_name: str
    type: str          # "int", "bool" or "list"
    node: ast.AST
    initial: int = 0
    size: int = 0


@dataclass
class Function:
    name: str
    node: ast.FunctionDef
    params: list[tuple[str, str]]   # (name, type)
    returns: str                    # "None", "int" or "bool"
    value: ast.expr | None = None   # the single ``return`` expression of a value function


@dataclass
class _Scope:
    function: Function
    prefix: str                                   # c names of locals: "<prefix>_<name>"
    locals: dict[str, Symbol] = field(default_factory=dict)
    declared_global: set[str] = field(default_factory=set)
    # A value function's parameters, while it is being copied into an expression.
    values: dict[str, Expr] = field(default_factory=dict)


def _wrap(expr: Expr, at_least: int) -> str:
    return expr.text if expr.prec >= at_least else f"({expr.text})"


def _int32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def _c_div(a: int, b: int) -> int:
    """Whole-number division the way C and the RISC-V CPU do it: toward zero; x // 0 is -1."""
    if b == 0:
        return -1
    q = abs(a) // abs(b)
    return _int32(q if (a < 0) == (b < 0) else -q)


def _c_mod(a: int, b: int) -> int:
    """The remainder that goes with _c_div: it has the sign of a; x % 0 is x."""
    if b == 0:
        return a
    return _int32(a - _c_div(a, b) * b)


# ---------------------------------------------------------------------------
# The compiler
# ---------------------------------------------------------------------------

class _Compiler:
    def __init__(self, source: str, project: dict[str, Any]):
        self.source = source
        self.project = project
        self.audio = project.get("audio_studio") or None
        self.globals: dict[str, Symbol] = {}
        self.c_names: dict[str, ast.AST] = {}     # every C name in use -> where it was declared
        self.state: dict[str, int] = {}           # C name -> initial value, in declaration order
        self.lists: dict[str, int] = {}
        self.functions: dict[str, Function] = {}
        self.features: set[str] = set()
        self.origins: dict[int, tuple[int, int, int, int]] = {}
        self.prelude: list[dict[str, Any]] = []   # list contents set before init runs
        self.statements = 0
        self.imported = False
        self.inlining: list[str] = []
        self.function_locals: dict[str, Symbol] = {}   # C name -> a local, shared by every call
        self.called: set[str] = set()                     # helpers copied in at least once

    # --- helpers --------------------------------------------------------------------------------
    def fail(self, code: str, message: str, node: ast.AST | None) -> PythonError:
        return PythonError(code, message, node)

    def stmt(self, node: ast.AST, ir: dict[str, Any]) -> dict[str, Any]:
        self.statements += 1
        if self.statements > MAX_IR_STATEMENTS:
            raise self.fail("too_big", say("py", "The game is too big: more than {max_ir_statements} statements after helper functions are copied in", max_ir_statements=MAX_IR_STATEMENTS), node)
        self.origins[id(ir)] = (node.lineno, node.col_offset + 1, node.end_lineno or node.lineno,
                                (node.end_col_offset or node.col_offset) + 1)
        return ir

    def claim(self, c_name: str, node: ast.AST, python_name: str) -> None:
        if c_name in self.c_names:
            raise self.fail("name_taken", say("py", "The name {python_name} is already used (as {c_name} in the generated C); choose another name", python_name=repr(python_name), c_name=c_name), node)
        if len(self.c_names) >= MAX_SYMBOLS:
            raise self.fail("too_many_names", say("py", "More than {max_symbols} variables", max_symbols=MAX_SYMBOLS), node)
        self.c_names[c_name] = node

    def check_name(self, name: str, node: ast.AST) -> str:
        """The C name of a student name, which must be the same word in lower case."""
        if not name.isascii():
            raise self.fail("name_ascii", say("py", "Use only English letters, digits and _ in names: {name}", name=repr(name)), node)
        if name in _BUILTIN_NAMES or name in LIFECYCLE and not isinstance(node, ast.FunctionDef):
            raise self.fail("name_reserved", say("py", "{name} already means something in PRG32 Python; choose another name", name=repr(name)),
                            node)
        c_name = state_name(name)
        if c_name != name.lower() or c_name.startswith("prg32"):
            raise self.fail("name_reserved", say("py", "{name} cannot be a name: the generated C uses it. Choose another name", name=repr(name)), node)
        if name.startswith("_"):
            raise self.fail("name_underscore", say("py", "Names start with a letter: {name}", name=repr(name)), node)
        return c_name

    def annotation(self, node: ast.expr | None, allow_list: bool = False) -> str:
        if isinstance(node, ast.Name) and node.id in TYPES:
            return node.id
        if allow_list and isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name) \
                and node.value.id == "list" and isinstance(node.slice, ast.Name) and node.slice.id == "int":
            return "list"
        what = "int, bool or list[int]" if allow_list else "int or bool"
        raise self.fail("type_annotation", say("py", "Give a type here: {what}", what=what), node)

    # --- the whole program ------------------------------------------------------------------------
    def program(self, tree: ast.Module) -> dict[str, Any]:
        docstring = self.declare(tree)
        for name in LIFECYCLE:
            if name not in self.functions:
                raise PythonError("lifecycle_missing", say("py", "Define the function  def {name}() -> None:", name=name),
                                  line=1, column=1, end_line=1, end_column=1)
        phases = {name: self.lifecycle(self.functions[name]) for name in LIFECYCLE}
        # A helper nobody calls yet is still checked, in a separate compiler so the game is unchanged.
        for name, function in self.functions.items():
            if name not in LIFECYCLE and name not in self.called:
                checker = _Compiler(self.source, self.project)
                checker.declare(tree)
                checker.check_unused(checker.functions[name])
        title = self.project.get("title") or "PRG32 Game"
        prefix = c_identifier(self.project.get("entry_prefix") or title, "prg32_game")
        description = self.project.get("description") or ""
        if not description and docstring.strip():
            description = docstring.strip().splitlines()[0][:200]
        return {
            "abi": IR_ABI,
            "title": title,
            "description": description,
            "author": self.project.get("author", "Student"),
            "entry_prefix": prefix,
            "state": [{"name": name, "type": "int", "initial": initial} for name, initial in self.state.items()],
            "lists": [{"name": name, "size": size} for name, size in self.lists.items()],
            "required_features": sorted(self.features),
            "init": self.prelude + phases["init"],
            "update": phases["update"],
            "draw": phases["draw"],
            "warnings": [],
            "source_mode": "python",
            "python_language_version": LANGUAGE_VERSION,
        }

    def declare(self, tree: ast.Module) -> str:
        """Read the top level: the import, the typed variables and the functions. Returns the docstring."""
        body = list(tree.body)
        docstring = ""
        if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant) \
                and isinstance(body[0].value.value, str):
            docstring = body.pop(0).value.value
        for node in body:
            if isinstance(node, ast.Import):
                self.module_import(node)
            elif isinstance(node, ast.AnnAssign):
                self.module_variable(node)
            elif isinstance(node, ast.FunctionDef):
                self.declare_function(node)
            elif isinstance(node, ast.ImportFrom):
                raise self.fail("import", say("py", "Write 'import prg32' and then prg32.name(...): 'from ... import' is not part of PRG32 Python"), node)
            elif isinstance(node, ast.Assign):
                raise self.fail("type_annotation", say("py", "Give the variable a type, for example  score: int = 0"), node)
            elif isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
                continue  # a text used as a comment between functions
            else:
                raise self.fail("module_statement", say("py", "Outside functions, PRG32 Python has only 'import prg32', typed variables and 'def'"), node)
        return docstring

    def check_unused(self, function: Function) -> None:
        """Check a helper that no lifecycle function calls, as if it were called once."""
        self.inlining.append(function.name)
        inner = _Scope(function, function.name)
        if function.value is not None:
            for pname, ptype in function.params:
                inner.values[pname] = Expr(state_name(f"{function.name}_{pname}"), ptype, _ATOM)
            self.expect(self.expression(function.value, inner), function.returns, function.value,
                        f"{function.name}'s result")
            return
        for pname, ptype in function.params:
            inner.locals[pname] = self.param_symbol(function, pname, ptype)
        self.body(function.node.body, inner, 1, top=True)

    def module_import(self, node: ast.Import) -> None:
        if len(node.names) != 1 or node.names[0].name != "prg32" or node.names[0].asname:
            raise self.fail("import", say("py", "PRG32 Python imports only one module:  import prg32"), node)
        self.imported = True

    def module_variable(self, node: ast.AnnAssign) -> None:
        if not isinstance(node.target, ast.Name):
            raise self.fail("assign_target", say("py", "Declare one variable by name, for example  score: int = 0"), node)
        kind = self.annotation(node.annotation, allow_list=True)
        name = node.target.id
        c_name = self.check_name(name, node.target)
        if node.value is None:
            raise self.fail("initial_value", f"Give {name} a starting value, for example  {name}: {kind} = 0"
                            if kind != "list" else f"Give {name} a size, for example  {name}: list[int] = [0] * 10",
                            node)
        if name in self.globals or name in self.functions:
            raise self.fail("name_taken", say("py", "{name} is declared twice", name=name), node.target)
        self.claim(c_name, node.target, name)
        if kind == "list":
            size, values = self.list_value(node.value)
            self.lists[c_name] = size
            symbol = Symbol(c_name, "list", node, size=size)
            if isinstance(values, int):
                if values:
                    self.prelude.append(self.stmt(node, {"op": "list_fill", "list": c_name, "value": str(values)}))
            else:
                for index, value in enumerate(values):
                    if value:
                        self.prelude.append(self.stmt(node, {"op": "list_set", "list": c_name, "index": str(index),
                                                             "value": str(value)}))
        else:
            value, value_type = self.constant(node.value)
            if value_type != kind:
                raise self.fail("type_mismatch", say("py", "{name} is {kind} but its starting value is {value_type}", name=name, kind=kind, value_type=value_type), node.value)
            symbol = Symbol(c_name, kind, node, initial=value)
            self.state[c_name] = value
        self.globals[name] = symbol

    def list_value(self, node: ast.expr) -> tuple[int, int | list[int]]:
        """[v] * n -> (n, v); [a, b, c] -> (3, [a, b, c])."""
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult) and isinstance(node.left, ast.List) \
                and len(node.left.elts) == 1:
            fill, fill_type = self.constant(node.left.elts[0])
            size, size_type = self.constant(node.right)
            if fill_type != "int" or size_type != "int":
                raise self.fail("list_value", say("py", "A list holds whole numbers: [0] * 10"), node)
            if not 1 <= size <= MAX_LIST_SIZE:
                raise self.fail("list_size", say("py", "A list has 1 to {max_list_size} items", max_list_size=MAX_LIST_SIZE), node.right)
            return size, fill
        if isinstance(node, ast.List):
            if not 1 <= len(node.elts) <= MAX_LIST_LITERAL:
                raise self.fail("list_size", say("py", "Write 1 to {max_list_literal} items in brackets, or use [0] * n", max_list_literal=MAX_LIST_LITERAL), node)
            values = []
            for item in node.elts:
                value, value_type = self.constant(item)
                if value_type != "int":
                    raise self.fail("list_value", say("py", "A list holds whole numbers"), item)
                values.append(value)
            return len(values), values
        raise self.fail("list_value", say("py", "Make a list with [0] * size or [1, 2, 3]"), node)

    def constant(self, node: ast.expr) -> tuple[int, str]:
        """A starting value: numbers, True/False, prg32 constants and earlier variables' starting values."""
        if isinstance(node, ast.Constant) and isinstance(node.value, bool):
            return int(node.value), "bool"
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            if node.value > MAX_INT:
                raise self.fail("number_range", say("py", "{value} is larger than {max_int}, the largest int", value=node.value, max_int=MAX_INT), node)
            return node.value, "int"
        if isinstance(node, ast.Attribute):
            return CONSTANTS[self.prg32_constant(node)], "int"
        if isinstance(node, ast.Name) and node.id in self.globals and self.globals[node.id].type in TYPES:
            symbol = self.globals[node.id]
            return symbol.initial, symbol.type
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd, ast.Invert)):
            value, value_type = self.constant(node.operand)
            if value_type != "int":
                raise self.fail("type_mismatch", say("py", "This needs a whole number"), node.operand)
            return _int32(-value if isinstance(node.op, ast.USub) else ~value if isinstance(node.op, ast.Invert)
                          else value), "int"
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
            value, value_type = self.constant(node.operand)
            if value_type != "bool":
                raise self.fail("type_mismatch", say("py", "'not' needs True or False"), node.operand)
            return int(not value), "bool"
        if isinstance(node, ast.BinOp) and type(node.op) in _BINOPS:
            a, a_type = self.constant(node.left)
            b, b_type = self.constant(node.right)
            if a_type != "int" or b_type != "int":
                raise self.fail("type_mismatch", say("py", "Arithmetic needs whole numbers"), node)
            op = _BINOPS[type(node.op)]
            result = {"+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b, "/": lambda: _c_div(a, b),
                      "%": lambda: _c_mod(a, b), "<<": lambda: a << (b & 31), ">>": lambda: a >> (b & 31),
                      "&": lambda: a & b, "|": lambda: a | b, "^": lambda: a ^ b}[op]()
            return _int32(result), "int"
        if isinstance(node, ast.BinOp) and type(node.op) in _REFUSED_BINOPS:
            raise self.fail("operator", _REFUSED_BINOPS[type(node.op)], node)
        raise self.fail("constant", say("py", "A starting value must be a fixed number, True/False, a prg32 constant or an earlier variable"), node)

    def declare_function(self, node: ast.FunctionDef) -> None:
        name = node.name
        if len(self.functions) >= MAX_FUNCTIONS:
            raise self.fail("too_many_functions", say("py", "More than {max_functions} functions", max_functions=MAX_FUNCTIONS), node)
        if name in self.functions or name in self.globals:
            raise self.fail("name_taken", say("py", "{name} is defined twice", name=name), node)
        if name not in LIFECYCLE:
            if name in VALUE_FUNCTIONS or name in STATEMENTS:
                raise self.fail("name_reserved", say("py", "{name} is a prg32 function name; choose another name", name=repr(name)), node)
            self.claim_function(name, node)
        if node.decorator_list:
            raise self.fail("decorator", say("py", "Decorators (@...) are not part of PRG32 Python"), node.decorator_list[0])
        if getattr(node, "type_params", None):
            raise self.fail("generic", say("py", "Type parameters are not part of PRG32 Python"), node)
        args = node.args
        if args.vararg or args.kwarg or args.kwonlyargs or args.posonlyargs or args.defaults or args.kw_defaults:
            raise self.fail("parameters", say("py", "Parameters are plain names with a type: def move(dx: int, dy: int) -> None:"),
                            node)
        params = []
        for arg in args.args:
            params.append((arg.arg, self.annotation(arg.annotation)))
            self.check_name(arg.arg, arg)
        if len({p for p, _ in params}) != len(params):
            raise self.fail("parameters", say("py", "Two parameters have the same name"), node)
        if node.returns is None:
            raise self.fail("return_type", say("py", "Say what {name} gives back:  -> None  (nothing), -> int or -> bool", name=name),
                            node)
        if isinstance(node.returns, ast.Constant) and node.returns.value is None:
            returns = "None"
        else:
            returns = self.annotation(node.returns)
        function = Function(name, node, params, returns)
        if name in LIFECYCLE and (params or returns != "None"):
            raise self.fail("lifecycle_signature", say("py", "Write it as  def {name}() -> None:", name=name), node)
        if returns != "None":
            body = [stmt for stmt in node.body if not _is_docstring(stmt)]
            if len(body) != 1 or not isinstance(body[0], ast.Return) or body[0].value is None:
                raise self.fail("value_function", say("py", "A function that gives back {returns} has one line:  return <expression>", returns=returns), node)
            function.value = body[0].value
        self.functions[name] = function

    def claim_function(self, name: str, node: ast.FunctionDef) -> None:
        """Helper functions are copied where they are called, so their names never reach the C."""
        if not name.isascii() or name.startswith("_"):
            raise self.fail("name_ascii", say("py", "Function names use English letters, digits and _, and start with a letter: {name}", name=repr(name)), node)
        if name in _BUILTIN_NAMES:
            raise self.fail("name_reserved", say("py", "{name} already means something in PRG32 Python; choose another name", name=repr(name)),
                            node)

    # --- statements -----------------------------------------------------------------------------
    def lifecycle(self, function: Function) -> list[dict[str, Any]]:
        scope = _Scope(function, function.name)
        return self.body(function.node.body, scope, depth=0, top=True)

    def body(self, statements: list[ast.stmt], scope: _Scope, depth: int, top: bool = False) -> list[dict[str, Any]]:
        if depth > MAX_BLOCK_DEPTH:
            raise self.fail("too_deep", say("py", "More than {max_block_depth} blocks inside each other", max_block_depth=MAX_BLOCK_DEPTH), statements[0])
        out: list[dict[str, Any]] = []
        for index, node in enumerate(statements):
            if isinstance(node, ast.Global):
                if not top:
                    raise self.fail("global", say("py", "Write 'global' at the start of the function"), node)
                for name in node.names:
                    if name not in self.globals:
                        raise self.fail("unknown_name", say("py", "There is no variable {name} outside the functions", name=name), node)
                    if name in scope.locals:
                        raise self.fail("global", say("py", "{name} is already a local variable here", name=name), node)
                    scope.declared_global.add(name)
                continue
            if isinstance(node, ast.Return):
                last = top and index == len(statements) - 1
                if node.value is not None or not last:
                    raise self.fail("return", say("py", "'return' can only be the last line of a function that gives back nothing (-> None)"), node)
                continue
            out.extend(self.statement(node, scope, depth))
        return out

    def statement(self, node: ast.stmt, scope: _Scope, depth: int) -> list[dict[str, Any]]:
        if isinstance(node, ast.Pass):
            return []
        if isinstance(node, ast.Expr):
            if isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
                return [self.stmt(node, {"op": "comment", "text": node.value.value.strip()[:200]})]
            if isinstance(node.value, ast.Call):
                return self.call_statement(node.value, node, scope, depth)
            if isinstance(node.value, (ast.Yield, ast.YieldFrom, ast.Await)):
                self.expression(node.value, scope)
            raise self.fail("useless_expression", say("py", "This line computes a value but does nothing with it"), node)
        if isinstance(node, ast.AnnAssign):
            return self.local_declaration(node, scope)
        if isinstance(node, ast.Assign):
            if len(node.targets) != 1:
                raise self.fail("assign_target", say("py", "Assign to one variable at a time"), node)
            return self.assign(node.targets[0], node.value, node, scope)
        if isinstance(node, ast.AugAssign):
            return self.augmented(node, scope)
        if isinstance(node, ast.If):
            return self.if_statement(node, scope, depth)
        if isinstance(node, ast.For):
            return self.for_statement(node, scope, depth)
        hints = {
            ast.While: "'while' loops could run forever; use  for i in range(n):  (at most 1024 rounds)",
            ast.Break: "'break' is not part of PRG32 Python yet: use an if inside the loop",
            ast.Continue: "'continue' is not part of PRG32 Python yet: use an if inside the loop",
            ast.Import: "Write 'import prg32' once, at the top of the file",
            ast.ImportFrom: "Write 'import prg32' once, at the top of the file",
            ast.FunctionDef: "Define functions at the top level of the file, not inside another function",
            ast.ClassDef: "Classes are not part of PRG32 Python",
            ast.Try: "try/except is not part of PRG32 Python",
            ast.With: "'with' is not part of PRG32 Python",
            ast.Delete: "'del' is not part of PRG32 Python",
            ast.Raise: "'raise' is not part of PRG32 Python",
            ast.Assert: "'assert' is not part of PRG32 Python",
            ast.Nonlocal: "'nonlocal' is not part of PRG32 Python",
            ast.AsyncFunctionDef: "async functions are not part of PRG32 Python",
            ast.AsyncFor: "async loops are not part of PRG32 Python",
            ast.AsyncWith: "async is not part of PRG32 Python",
            ast.Match: "'match' is not part of PRG32 Python: use if/elif",
        }
        for kind, hint in hints.items():
            if isinstance(node, kind):
                raise self.fail("statement", hint, node)
        raise self.fail("statement", say("py", "{name} is not part of PRG32 Python", name=type(node).__name__), node)

    def local_declaration(self, node: ast.AnnAssign, scope: _Scope) -> list[dict[str, Any]]:
        if not isinstance(node.target, ast.Name):
            raise self.fail("assign_target", say("py", "Declare one variable by name, for example  n: int = 0"), node)
        kind = self.annotation(node.annotation)
        name = node.target.id
        self.check_name(name, node.target)
        if node.value is None:
            raise self.fail("initial_value", say("py", "Give {name} a starting value:  {name}: {kind} = 0", name=name, kind=kind), node)
        if name in scope.locals or name in scope.declared_global or name in dict(scope.function.params):
            raise self.fail("name_taken", say("py", "{name} is already declared in {name2}", name=name, name2=scope.function.name), node.target)
        if name in self.globals:
            raise self.fail("name_taken", say("py", "{name} is also a variable outside the functions: choose another name (or write  global {name}  and leave out the type)", name=name), node.target)
        symbol = self.new_local(scope, name, kind, node.target)
        return self.assign(node.target, node.value, node, scope, symbol)

    def new_local(self, scope: _Scope, name: str, kind: str, node: ast.AST) -> Symbol:
        """A function's local variable: a variable of the game named <function>_<name>.

        A helper function copied in twice declares its locals twice: both copies share one variable.
        """
        c_name = state_name(f"{scope.prefix}_{name}")
        symbol = self.function_locals.get(c_name)
        if symbol is None:
            self.claim(c_name, node, name)
            symbol = self.function_locals[c_name] = Symbol(c_name, kind, node)
            self.state[c_name] = 0
        scope.locals[name] = symbol
        return symbol

    def variable(self, name: str, node: ast.AST, scope: _Scope, writing: bool) -> Symbol:
        if name in scope.locals:
            return scope.locals[name]
        if name in self.globals:
            if writing and self.globals[name].type in TYPES and name not in scope.declared_global:
                raise self.fail("global", say("py", "To change {name} inside {name2}, write  global {name}  at the start of the function", name=name, name2=scope.function.name), node)
            return self.globals[name]
        if name in scope.values or name in dict(scope.function.params):
            raise self.fail("parameter", say("py", "{name} is a parameter: it cannot be changed", name=name), node)
        raise self.fail("unknown_name", say("py", "{name} is not defined. Declare it first, for example  {name}: int = 0", name=name),
                        node)

    def assign(self, target: ast.expr, value: ast.expr, node: ast.stmt, scope: _Scope,
               symbol: Symbol | None = None) -> list[dict[str, Any]]:
        if isinstance(target, ast.Name):
            symbol = symbol or self.variable(target.id, target, scope, writing=True)
            if symbol.type == "list":
                raise self.fail("list_assign", say("py", "{id} is a list: change one item, {id}[i] = ...", id=target.id), target)
            expr = self.expression(value, scope)
            self.expect(expr, symbol.type, value)
            return [self.stmt(node, {"op": "set", "var": symbol.c_name, "value": self.ir_text(expr, value)})]
        if isinstance(target, ast.Subscript):
            symbol, index = self.list_item(target, scope)
            expr = self.expression(value, scope)
            self.expect(expr, "int", value)
            return [self.stmt(node, {"op": "list_set", "list": symbol.c_name, "index": index,
                                     "value": self.ir_text(expr, value)})]
        if isinstance(target, (ast.Tuple, ast.List)):
            raise self.fail("assign_target", say("py", "Assign one variable at a time (no a, b = ...)"), target)
        raise self.fail("assign_target", say("py", "You can assign to a variable or to a list item"), target)

    def list_item(self, target: ast.Subscript, scope: _Scope) -> tuple[Symbol, str]:
        if not isinstance(target.value, ast.Name):
            raise self.fail("list_item", say("py", "Write list[position]"), target)
        symbol = self.variable(target.value.id, target.value, scope, writing=False)
        if symbol.type != "list":
            raise self.fail("not_a_list", say("py", "{id} is not a list", id=target.value.id), target.value)
        if isinstance(target.slice, ast.Slice):
            raise self.fail("slice", say("py", "Slices (a[1:3]) are not part of PRG32 Python"), target.slice)
        index = self.expression(target.slice, scope)
        self.expect(index, "int", target.slice)
        return symbol, self.ir_text(index, target.slice)

    def augmented(self, node: ast.AugAssign, scope: _Scope) -> list[dict[str, Any]]:
        if type(node.op) in _REFUSED_BINOPS:
            raise self.fail("operator", _REFUSED_BINOPS[type(node.op)], node)
        op = _BINOPS[type(node.op)]
        value = self.expression(node.value, scope)
        self.expect(value, "int", node.value)
        if isinstance(node.target, ast.Name):
            symbol = self.variable(node.target.id, node.target, scope, writing=True)
            if symbol.type != "int":
                raise self.fail("type_mismatch", say("py", "{id} is {type}: '{op}=' needs an int", id=node.target.id, type=symbol.type, op=op), node.target)
            if op in "+-":
                delta = value if op == "+" else Expr(f"-{_wrap(value, _UNARY_PREC)}", "int", _UNARY_PREC, value.impure)
                return [self.stmt(node, {"op": "change", "var": symbol.c_name, "delta": self.ir_text(delta, node)})]
            current = Expr(symbol.c_name, "int", _ATOM)
            return [self.stmt(node, {"op": "set", "var": symbol.c_name,
                                     "value": self.ir_text(self.binary(op, current, value), node)})]
        if isinstance(node.target, ast.Subscript):
            symbol, index = self.list_item(node.target, scope)
            if op in "+-":
                delta = value if op == "+" else Expr(f"-{_wrap(value, _UNARY_PREC)}", "int", _UNARY_PREC, value.impure)
                return [self.stmt(node, {"op": "list_change", "list": symbol.c_name, "index": index,
                                         "delta": self.ir_text(delta, node)})]
            current = Expr(f"{symbol.c_name}[{index}]", "int", _ATOM)
            return [self.stmt(node, {"op": "list_set", "list": symbol.c_name, "index": index,
                                     "value": self.ir_text(self.binary(op, current, value), node)})]
        raise self.fail("assign_target", say("py", "You can change a variable or a list item"), node.target)

    def if_statement(self, node: ast.If, scope: _Scope, depth: int) -> list[dict[str, Any]]:
        parts = node.test.values if isinstance(node.test, ast.BoolOp) and isinstance(node.test.op, ast.And) \
            else [node.test]
        specials = [part for part in parts if self.is_condition(part)]
        if not specials:
            cond = self.expression(node.test, scope)
            result = {"op": "if", "cond": self.ir_text(cond, node.test),
                      "then": self.body(node.body, scope, depth + 1)}
            if node.orelse:
                result["else"] = self.body(node.orelse, scope, depth + 1)
            return [self.stmt(node, result)]
        if node.orelse:
            raise self.fail("condition_else", say("py", "An if that tests prg32.held, prg32.pressed or prg32.touching cannot have elif/else yet: write a second if"), node.orelse[0])
        # "if held(A) and x > 0:" becomes nested ifs, tested left to right like Python's 'and'.
        body = self.body(node.body, scope, depth + 1)
        plain: list[ast.expr] = []
        layers: list[tuple[ast.expr, bool]] = []
        for part in parts:
            if self.is_condition(part):
                if plain:
                    layers.append((_joined(plain), False))
                    plain = []
                layers.append((part, True))
            else:
                plain.append(part)
        if plain:
            layers.append((_joined(plain), False))
        for part, special in reversed(layers):
            if special:
                ir = self.condition(part, scope)
                ir["then"] = body
            else:
                cond = self.expression(part, scope)
                ir = {"op": "if", "cond": self.ir_text(cond, part), "then": body}
            body = [self.stmt(node, ir)]
        return body

    def is_condition(self, node: ast.expr) -> bool:
        return isinstance(node, ast.Call) and self.prg32_name(node.func) in CONDITIONS

    def condition(self, call: ast.Call, scope: _Scope) -> dict[str, Any]:
        name = self.prg32_name(call.func)
        op, params, _help = CONDITIONS[name]
        ir: dict[str, Any] = {"op": op}
        ir.update(self.arguments(name, params, call, scope))
        if name == "touching":
            self.features.add("sprites")
        return ir

    def for_statement(self, node: ast.For, scope: _Scope, depth: int) -> list[dict[str, Any]]:
        if node.orelse:
            raise self.fail("for_else", say("py", "'for ... else' is not part of PRG32 Python"), node.orelse[0])
        if not isinstance(node.target, ast.Name):
            raise self.fail("for_target", say("py", "Count with one name:  for i in range(10):"), node.target)
        call = node.iter
        if not (isinstance(call, ast.Call) and isinstance(call.func, ast.Name) and call.func.id == "range"):
            raise self.fail("for_range", say("py", "Loop with range:  for i in range(10):  or  for i in range(2, 8):"), node.iter)
        if call.keywords or not 1 <= len(call.args) <= 2:
            raise self.fail("for_range", say("py", "range takes one or two numbers (no step yet): range(stop) or range(start, stop)"),
                            call)
        start = self.expression(call.args[0], scope) if len(call.args) == 2 else Expr("0", "int", _ATOM)
        stop = self.expression(call.args[-1], scope)
        self.expect(start, "int", call.args[0])
        self.expect(stop, "int", call.args[-1])
        if not start.text.lstrip("-").isdigit() or not stop.text.lstrip("-").isdigit():
            pass  # bounds known only while the game runs: the generated loop stops after MAX_REPEAT rounds
        elif int(stop.text) - int(start.text) > MAX_REPEAT:
            raise self.fail("loop_bound", say("py", "A loop runs at most {max_repeat} rounds", max_repeat=MAX_REPEAT), call)
        name = node.target.id
        self.check_name(name, node.target)
        if name in scope.locals or name in scope.declared_global:
            symbol = self.variable(name, node.target, scope, writing=True)
        elif name in self.globals:
            raise self.fail("global", say("py", "{name} is a variable outside the functions: write  global {name}  first, or count with another name", name=name), node.target)
        elif name in dict(scope.function.params) or name in scope.values:
            raise self.fail("parameter", say("py", "{name} is a parameter: count with another name", name=name), node.target)
        else:
            symbol = self.new_local(scope, name, "int", node.target)
        if symbol.type != "int":
            raise self.fail("type_mismatch", say("py", "{name} must be an int to count", name=name), node.target)
        last = Expr(str(int(stop.text) - 1), "int", _ATOM) if stop.text.lstrip("-").isdigit() \
            else self.binary("-", stop, Expr("1", "int", _ATOM))
        ir = {"op": "repeat", "var": symbol.c_name, "from": self.ir_text(start, call), "to": self.ir_text(last, call),
              "body": self.body(node.body, scope, depth + 1)}
        return [self.stmt(node, ir)]

    def call_statement(self, call: ast.Call, node: ast.stmt, scope: _Scope, depth: int) -> list[dict[str, Any]]:
        name = self.prg32_name(call.func)
        if name is not None:
            if name in STATEMENTS:
                op, params, feature, _help = STATEMENTS[name]
                ir: dict[str, Any] = {"op": op}
                ir.update(self.arguments(name, params, call, scope))
                if feature:
                    self.features.add(feature)
                if op in ("sfx", "track_play"):
                    ir = self.audio_statement(ir, call)
                if op == "band" and ir.get("value") == "":
                    del ir["value"]
                return [self.stmt(node, ir)]
            if name in CONDITIONS:
                raise self.fail("condition_use", say("py", "prg32.{name}(...) is a test: use it in an if", name=name), call)
            if name not in VALUE_FUNCTIONS:
                raise self.fail("unknown_function", say("py", "prg32 has no function {name}", name=name), call.func)
            raise self.fail("value_unused", say("py", "prg32.{name}(...) gives a value: store it, for example  x = prg32.{name}(...)", name=name),
                            call)
        if isinstance(call.func, ast.Name):
            if call.func.id == "print":
                raise self.fail("print", say("py", "PRG32 has no console: show text with prg32.text(x, y, \"HI\") or numbers with prg32.number(value, x, y)"), call)
            function = self.functions.get(call.func.id)
            if function is None:
                raise self.fail("unknown_function", say("py", "There is no function {id}", id=call.func.id), call.func)
            if function.name in LIFECYCLE:
                raise self.fail("lifecycle_call", say("py", "The PRG32 calls {name}() itself: do not call it", name=function.name), call)
            if function.returns != "None":
                raise self.fail("value_unused", say("py", "{name} gives back a value: use it, for example  x = {name}(...)", name=function.name), call)
            return self.inline_procedure(function, call, node, scope, depth)
        raise self.fail("call", say("py", "Call prg32.name(...) or one of your own functions"), call)

    def inline_procedure(self, function: Function, call: ast.Call, node: ast.stmt, scope: _Scope,
                         depth: int) -> list[dict[str, Any]]:
        """A helper function is copied where it is called; its parameters become variables."""
        self.enter(function, call)
        try:
            args = self.call_args(function, call, scope)
            inner = _Scope(function, function.name)
            out = [self.stmt(node, {"op": "comment", "text": f"{function.name}(): its parameters, then its lines"})]
            for (pname, ptype), (arg_node, expr) in zip(function.params, args):
                symbol = self.param_symbol(function, pname, ptype)
                inner.locals[pname] = symbol
                out.append(self.stmt(node, {"op": "set", "var": symbol.c_name, "value": self.ir_text(expr, arg_node)}))
            out.extend(self.body(function.node.body, inner, depth + 1, top=True))
            return out
        finally:
            self.inlining.pop()

    def param_symbol(self, function: Function, name: str, kind: str) -> Symbol:
        """The variable that holds one parameter of a helper (shared by every call: no recursion)."""
        c_name = state_name(f"{function.name}_{name}")
        if c_name not in self.state:
            self.claim(c_name, function.node, name)
            self.state[c_name] = 0
        return Symbol(c_name, kind, function.node)

    def enter(self, function: Function, call: ast.Call) -> None:
        if function.name in self.inlining:
            raise self.fail("recursion", say("py", "{name} calls itself (through {value}): PRG32 Python has no recursion", name=function.name, value=' -> '.join(self.inlining)), call)
        if len(self.inlining) >= MAX_INLINE_DEPTH:
            raise self.fail("too_deep", say("py", "Functions call each other more than {max_inline_depth} deep", max_inline_depth=MAX_INLINE_DEPTH), call)
        self.inlining.append(function.name)
        self.called.add(function.name)

    def call_args(self, function: Function, call: ast.Call, scope: _Scope) -> list[tuple[ast.expr, Expr]]:
        if call.keywords or any(isinstance(arg, ast.Starred) for arg in call.args):
            raise self.fail("call_arguments", say("py", "Give {name} its values in order, without names", name=function.name), call)
        if len(call.args) != len(function.params):
            raise self.fail("call_arguments", say("py", "{name} needs {count} value(s), not {count2}", name=function.name, count=len(function.params), count2=len(call.args)), call)
        out = []
        for arg, (pname, ptype) in zip(call.args, function.params):
            expr = self.expression(arg, scope)
            self.expect(expr, ptype, arg, f"{pname} of {function.name}")
            out.append((arg, expr))
        return out

    def audio_statement(self, ir: dict[str, Any], call: ast.Call) -> dict[str, Any]:
        """AudioStudio names become fixed numbers from the project's document, as for the Blocks."""
        audio = self.audio or {"effects": [], "soundtracks": []}
        name = ir["name"]
        if ir["op"] == "sfx":
            index = next((i for i, item in enumerate(audio.get("effects", [])) if item.get("name") == name), None)
            if index is None:
                raise self.fail("missing_sound", say("py", "This project's AudioStudio has no sound effect named {name}", name=repr(name)), call)
            effect = audio["effects"][index]
            return {"op": "sfx", "name": name, "voice": effect["voice"], "instrument": 8 + index,
                    "note": effect["note"], "volume": effect["volume"], "ms": effect["duration_ms"]}
        index = next((i for i, item in enumerate(audio.get("soundtracks", [])) if item.get("name") == name), None)
        if index is None:
            raise self.fail("missing_sound", say("py", "This project's AudioStudio has no soundtrack named {name}", name=repr(name)), call)
        return {"op": "track_play", "name": name, "track": index}

    def arguments(self, name: str, params: list[_P], call: ast.Call, scope: _Scope) -> dict[str, Any]:
        """Match a prg32 call's values to its parameters (in order or by name) and check each one."""
        if any(isinstance(arg, ast.Starred) for arg in call.args) or any(k.arg is None for k in call.keywords):
            raise self.fail("call_arguments", say("py", "Give each value on its own (no * or **)"), call)
        if len(call.args) > len(params):
            raise self.fail("call_arguments", say("py", "prg32.{name} takes at most {count} values", name=name, count=len(params)), call)
        given: dict[str, ast.expr] = {params[i][0]: arg for i, arg in enumerate(call.args)}
        for keyword in call.keywords:
            if keyword.arg not in {p[0] for p in params}:
                raise self.fail("call_arguments", say("py", "prg32.{name} has no value called {arg}", name=name, arg=keyword.arg), keyword)
            if keyword.arg in given:
                raise self.fail("call_arguments", say("py", "{arg} is given twice", arg=keyword.arg), keyword)
            given[keyword.arg] = keyword.value
        out: dict[str, Any] = {}
        for pname, key, kind, default in params:
            node = given.get(pname)
            if node is None:
                if default is None:
                    raise self.fail("call_arguments", say("py", "prg32.{name} needs {pname}", name=name, pname=pname), call)
                if kind == "flag":
                    out[key] = bool(default)
                elif kind == "int":
                    out[key] = "" if default == "" else str(default)
                else:
                    out[key] = default
                continue
            out[key] = self.argument(name, pname, kind, node, scope)
        return out

    def argument(self, name: str, pname: str, kind: str, node: ast.expr, scope: _Scope) -> Any:
        if kind == "int":
            expr = self.expression(node, scope)
            self.expect(expr, "int", node, pname)
            return self.ir_text(expr, node)
        if kind == "color":
            constant = self.prg32_name(node)
            if constant not in COLOR_NAMES:
                raise self.fail("color", f"{pname} is a colour such as prg32.RED: " + ", ".join(
                    f"prg32.{c}" for c in COLOR_NAMES[:6]) + ", ...", node)
            return constant
        if kind == "button":
            constant = self.prg32_name(node)
            if constant not in BUTTON_MAP:   # player 2's buttons too: prg32.P2_LEFT, ...
                raise self.fail("button", "Name a button: " + ", ".join(f"prg32.{b}" for b in BUTTON_NAMES)
                                + " (player 2: prg32.P2_LEFT, ...)", node)
            return constant
        if kind == "flag":
            if not (isinstance(node, ast.Constant) and isinstance(node.value, bool)):
                raise self.fail("flag", say("py", "{pname} is True or False", pname=pname), node)
            return node.value
        if kind in ("text", "effect", "track"):
            limit = _TEXT_LIMITS.get(name, MAX_TEXT) if kind == "text" else 32
            if not (isinstance(node, ast.Constant) and isinstance(node.value, str)):
                raise self.fail("text", say("py", "{pname} is a text in quotes, for example \"SCORE\"", pname=pname), node)
            text = node.value
            if len(text) > limit:
                raise self.fail("text", say("py", "{pname} has at most {limit} characters", pname=pname, limit=limit), node)
            if kind == "text" and any(not 32 <= ord(ch) <= 126 for ch in text):
                raise self.fail("text", say("py", "The PRG32 font has only English letters, digits and the usual signs"), node)
            return text
        if kind == "rows":
            if not isinstance(node, ast.List) or len(node.elts) != 8 or any(
                    not (isinstance(row, ast.Constant) and isinstance(row.value, str) and len(row.value) <= 8)
                    for row in node.elts):
                raise self.fail("rows", say("py", "A picture is 8 texts of up to 8 characters: [\"..XX....\", ...]"), node)
            rows = []
            for row in node.elts:
                value = 0
                for col, ch in enumerate(row.value.ljust(8, ".")):
                    if _ON_PIXEL.match(ch):
                        value |= 0x80 >> col
                rows.append(value)
            return rows
        raise AssertionError(kind)

    # --- expressions ----------------------------------------------------------------------------
    def expect(self, expr: Expr, wanted: str, node: ast.AST, what: str = "") -> None:
        if expr.type != wanted:
            hint = " (compare it, for example  x > 0)" if wanted == "bool" else \
                " (True/False are not numbers in PRG32 Python)" if wanted == "int" else ""
            message = f"{what} needs {wanted}, but this is {expr.type}" if what else \
                f"This needs {wanted}, but it is {expr.type}"
            raise self.fail("type_mismatch", message + hint, node)

    def ir_text(self, expr: Expr, node: ast.AST) -> str:
        """Check the text with the kit's expression grammar and store it the way the Blocks do."""
        try:
            parsed = parse_expr(expr.text, self.lists)
        except (ExpressionError, RecursionError) as exc:
            if len(expr.text) > 200:
                raise self.fail("expression_too_long", say("py", "This expression is too long (more than 200 characters after helper functions are copied in): split it with a variable"), node) from None
            raise self.fail("expression", say("py", "This expression cannot be translated: {exc}", exc=exc), node) from None
        self.features |= parsed.features
        return render(parsed.tree)

    def binary(self, op: str, left: Expr, right: Expr) -> Expr:
        prec = _PREC[op]
        return Expr(f"{_wrap(left, prec)} {op} {_wrap(right, prec + 1)}", "int", prec, left.impure or right.impure)

    def expression(self, node: ast.expr, scope: _Scope) -> Expr:
        if isinstance(node, ast.Constant):
            if isinstance(node.value, bool):
                return Expr("1" if node.value else "0", "bool", _ATOM)
            if isinstance(node.value, int):
                if node.value > MAX_INT:
                    raise self.fail("number_range", say("py", "{value} is larger than {max_int}, the largest int", value=node.value, max_int=MAX_INT), node)
                return Expr(str(node.value), "int", _ATOM)
            if isinstance(node.value, str):
                raise self.fail("text_value", say("py", "Texts can only be given to prg32.text, prg32.band and the like"), node)
            raise self.fail("number_kind", say("py", "PRG32 numbers are whole numbers (no 1.5, no None)"), node)
        if isinstance(node, ast.Name):
            if node.id in scope.values:
                return scope.values[node.id]
            if node.id in ("True", "False"):
                return Expr("1" if node.id == "True" else "0", "bool", _ATOM)
            symbol = self.variable(node.id, node, scope, writing=False)
            if symbol.type == "list":
                raise self.fail("list_value", say("py", "{id} is a list: read one item, {id}[i]", id=node.id), node)
            return Expr(symbol.c_name, symbol.type, _ATOM)
        if isinstance(node, ast.Attribute):
            return Expr(self.prg32_constant(node), "int", _ATOM)
        if isinstance(node, ast.Subscript):
            symbol, index = self.list_item(node, scope)
            return Expr(f"{symbol.c_name}[{index}]", "int", _ATOM)
        if isinstance(node, ast.BinOp):
            if type(node.op) in _REFUSED_BINOPS:
                raise self.fail("operator", _REFUSED_BINOPS[type(node.op)], node)
            left, right = self.expression(node.left, scope), self.expression(node.right, scope)
            op = _BINOPS[type(node.op)]
            for side, side_node in ((left, node.left), (right, node.right)):
                self.expect(side, "int", side_node)
            return self.binary(op, left, right)
        if isinstance(node, ast.UnaryOp):
            operand = self.expression(node.operand, scope)
            if isinstance(node.op, ast.Not):
                self.expect(operand, "bool", node.operand, "'not'")
                return Expr(f"!{_wrap(operand, _UNARY_PREC)}", "bool", _UNARY_PREC, operand.impure)
            self.expect(operand, "int", node.operand)
            if isinstance(node.op, ast.UAdd):
                return operand
            symbol = "-" if isinstance(node.op, ast.USub) else "~"
            inner = _wrap(operand, _UNARY_PREC)
            if inner.startswith(("-", "+")):
                inner = f"({inner})"
            return Expr(f"{symbol}{inner}", "int", _UNARY_PREC, operand.impure)
        if isinstance(node, ast.BoolOp):
            op = "&&" if isinstance(node.op, ast.And) else "||"
            word = "and" if op == "&&" else "or"
            parts = [self.expression(value, scope) for value in node.values]
            for part, value in zip(parts, node.values):
                if self.is_condition(value):
                    raise self.fail("condition_use", say("py", "prg32.{value}(...) can only be joined with 'and' directly in an if", value=self.prg32_name(value.func)), value)
                self.expect(part, "bool", value, f"'{word}'")
            prec = _PREC[op]
            text = f" {op} ".join(_wrap(part, prec + 1) for part in parts)
            return Expr(text, "bool", prec, any(part.impure for part in parts))
        if isinstance(node, ast.Compare):
            if len(node.ops) != 1:
                raise self.fail("compare_chain", say("py", "Compare two values at a time:  a < b and b < c"), node)
            if type(node.ops[0]) not in _COMPARE:
                raise self.fail("operator", say("py", "Compare with ==, !=, <, <=, > or >="), node)
            left, right = self.expression(node.left, scope), self.expression(node.comparators[0], scope)
            op = _COMPARE[type(node.ops[0])]
            if left.type != right.type or (left.type == "bool" and op not in ("==", "!=")):
                raise self.fail("type_mismatch", say("py", "Cannot compare {type} with {type2} using {op}", type=left.type, type2=right.type, op=op), node)
            prec = _PREC[op]
            return Expr(f"{_wrap(left, prec)} {op} {_wrap(right, prec + 1)}", "bool", prec,
                        left.impure or right.impure)
        if isinstance(node, ast.Call):
            return self.call_value(node, scope)
        hints = {ast.IfExp: "'a if c else b' is not part of PRG32 Python: use an if statement",
                 ast.Lambda: "lambda is not part of PRG32 Python", ast.ListComp: "List comprehensions are not part "
                 "of PRG32 Python", ast.NamedExpr: "':=' is not part of PRG32 Python",
                 ast.JoinedStr: "f-strings are not part of PRG32 Python", ast.Tuple: "Tuples are not part of PRG32 "
                 "Python", ast.List: "Lists are made once, outside the functions", ast.Dict: "Dictionaries are "
                 "not part of PRG32 Python", ast.Set: "Sets are not part of PRG32 Python",
                 ast.Await: "await is not part of PRG32 Python", ast.Yield: "Generators are not part of PRG32 Python",
                 ast.YieldFrom: "Generators are not part of PRG32 Python"}
        for kind, hint in hints.items():
            if isinstance(node, kind):
                raise self.fail("expression", hint, node)
        raise self.fail("expression", say("py", "{name} is not part of PRG32 Python", name=type(node).__name__), node)

    def call_value(self, call: ast.Call, scope: _Scope) -> Expr:
        name = self.prg32_name(call.func)
        if name is not None or isinstance(call.func, ast.Name) and call.func.id in BUILTIN_VALUES:
            builtin = name is None
            name = name if name is not None else call.func.id  # type: ignore[union-attr]
            if not builtin and name in CONDITIONS:
                raise self.fail("condition_use", say("py", "prg32.{name}(...) is a test: use it directly in an if", name=name), call)
            # prg32.frame is both: frame(x, y, w, h) draws an outline, frame() is the frame count.
            if not builtin and name in STATEMENTS and not (name in VALUE_FUNCTIONS and not call.args and not call.keywords):
                raise self.fail("value_expected", say("py", "prg32.{name}(...) does something but gives no value", name=name), call)
            if not builtin and name not in VALUE_FUNCTIONS:
                raise self.fail("unknown_function", say("py", "prg32 has no function {name}", name=name), call.func)
            arity = BUILTIN_VALUES[name] if builtin else VALUE_FUNCTIONS[name]
            if call.keywords or len(call.args) != arity:
                raise self.fail("call_arguments", say("py", "{name} needs {arity} value(s), in order", name=name, arity=arity), call)
            args = [self.expression(arg, scope) for arg in call.args]
            for arg, arg_node in zip(args, call.args):
                self.expect(arg, "int", arg_node)
            if name in FUNCTION_FEATURES:
                self.features.add(FUNCTION_FEATURES[name])
            text = f"{name}({', '.join(arg.text for arg in args)})"
            return Expr(text, "int", _ATOM, name in _IMPURE or any(arg.impure for arg in args))
        if isinstance(call.func, ast.Name):
            if call.func.id == "range":
                raise self.fail("range_use", say("py", "range(...) is only used in  for i in range(...):"), call)
            function = self.functions.get(call.func.id)
            if function is None:
                raise self.fail("unknown_function", say("py", "There is no function {id}", id=call.func.id), call.func)
            if function.returns == "None":
                raise self.fail("value_expected", say("py", "{name} gives back nothing (-> None)", name=function.name), call)
            return self.inline_value(function, call, scope)
        raise self.fail("call", say("py", "Call prg32.name(...), abs/min/max or one of your own functions"), call)

    def inline_value(self, function: Function, call: ast.Call, scope: _Scope) -> Expr:
        """A value function is copied into the expression, its parameters replaced by the values given."""
        self.enter(function, call)
        try:
            args = self.call_args(function, call, scope)
            uses = _name_uses(function.value)
            inner = _Scope(function, function.name)
            for (pname, _ptype), (arg_node, expr) in zip(function.params, args):
                if expr.impure and uses.get(pname, 0) != 1:
                    raise self.fail("impure_argument", say("py", "{pname} is used {value} times in {name}, so this value (it changes every time it is asked for) would not be the same each time: store it in a variable first", pname=pname, value=uses.get(pname, 0), name=function.name), arg_node)
                inner.values[pname] = Expr(f"({expr.text})" if expr.prec < _ATOM else expr.text, expr.type, _ATOM,
                                           expr.impure)
            result = self.expression(function.value, inner)
            self.expect(result, function.returns, function.value, f"{function.name}'s result")
            return Expr(f"({result.text})" if result.prec < _ATOM else result.text, result.type, _ATOM,
                        result.impure)
        finally:
            self.inlining.pop()

    def prg32_name(self, node: ast.expr) -> str | None:
        """prg32.<name> -> name (None for anything else)."""
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id == "prg32":
            if not self.imported:
                raise self.fail("import", say("py", "Write  import prg32  at the top of the file first"), node)
            return node.attr
        return None

    def prg32_constant(self, node: ast.Attribute) -> str:
        name = self.prg32_name(node)
        if name is None:
            raise self.fail("attribute", say("py", "Only prg32.NAME has a dot in PRG32 Python"), node)
        if name in CONSTANT_NAMES:
            return CONSTANT_NAMES[name]
        if name in VALUE_FUNCTIONS or name in STATEMENTS or name in CONDITIONS:
            raise self.fail("call_missing", say("py", "prg32.{name} is a function: call it with ( )", name=name), node)
        raise self.fail("unknown_constant", say("py", "prg32 has no constant {name}", name=name), node)


def _joined(parts: list[ast.expr]) -> ast.expr:
    """a and b and c as one expression, placed where its parts are in game.py."""
    if len(parts) == 1:
        return parts[0]
    joined = ast.BoolOp(op=ast.And(), values=parts)
    ast.copy_location(joined, parts[0])
    joined.end_lineno, joined.end_col_offset = parts[-1].end_lineno, parts[-1].end_col_offset
    return joined


def _is_docstring(node: ast.stmt) -> bool:
    return isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str)


def _name_uses(node: ast.AST | None) -> dict[str, int]:
    uses: dict[str, int] = {}
    for item in ast.walk(node) if node is not None else ():
        if isinstance(item, ast.Name):
            uses[item.id] = uses.get(item.id, 0) + 1
    return uses


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------

def _prescan(source: Any) -> str:
    """Limits checked before the parser sees the text."""
    if not isinstance(source, str) or not source.strip():
        raise PythonError("empty", say("py", "game.py is empty"))
    if "\x00" in source:
        raise PythonError("nul", say("py", "game.py contains a NUL character"))
    if len(source.encode("utf-8")) > MAX_SOURCE_BYTES:
        raise PythonError("too_big", say("py", "game.py is larger than {value} KiB", value=MAX_SOURCE_BYTES // 1024))
    if source.count("\n") + 1 > MAX_LINES:
        raise PythonError("too_big", say("py", "game.py has more than {max_lines} lines", max_lines=MAX_LINES))
    depth, line, quote = 0, 1, ""
    i = 0
    while i < len(source):
        ch = source[i]
        if ch == "\n":
            line += 1
        if quote:
            if ch == "\\":
                i += 2
                continue
            if source.startswith(quote, i):
                i += len(quote)
                quote = ""
                continue
        elif ch == "#":
            end = source.find("\n", i)
            i = len(source) if end < 0 else end
            continue
        elif ch in "\"'":
            quote = source[i:i + 3] if source[i:i + 3] in ('"""', "'''") else ch
            i += len(quote)
            continue
        elif ch in "([{":
            depth += 1
            if depth > MAX_BRACKET_DEPTH:
                raise PythonError("too_deep", say("py", "More than {max_bracket_depth} brackets inside each other", max_bracket_depth=MAX_BRACKET_DEPTH),
                                  line=line, column=1)
        elif ch in ")]}":
            depth = max(0, depth - 1)
        i += 1
    return source


def parse(source: Any) -> ast.Module:
    """Parse game.py into a syntax tree, within the limits. The program is never imported or run."""
    text = _prescan(source)
    try:
        tree = ast.parse(text, filename=SOURCE_FILE, mode="exec")
    except SyntaxError as exc:
        raise PythonError("syntax", say("py", "Python syntax: {msg}", msg=exc.msg), line=exc.lineno or 0, column=exc.offset or 0,
                          end_line=getattr(exc, "end_lineno", 0) or 0,
                          end_column=getattr(exc, "end_offset", 0) or 0) from None
    except (MemoryError, RecursionError, ValueError):
        raise PythonError("too_deep", say("py", "game.py is nested too deeply to read")) from None
    nodes = 0
    stack: list[tuple[ast.AST, int]] = [(tree, 0)]
    while stack:  # without recursion, so a deep tree cannot exhaust the stack here
        node, depth = stack.pop()
        nodes += 1
        if nodes > MAX_NODES:
            raise PythonError("too_big", say("py", "game.py has more than {max_nodes} parts", max_nodes=MAX_NODES))
        if depth > MAX_AST_DEPTH:
            raise PythonError("too_deep", say("py", "This expression or block is nested too deeply"), node)
        stack.extend((child, depth + 1) for child in ast.iter_child_nodes(node))
    return tree


def python_to_ir_traced(source: Any, project: dict[str, Any] | None = None
                        ) -> tuple[dict[str, Any], dict[int, tuple[int, int, int, int]]]:
    """game.py -> (game IR, id(IR statement) -> (line, column, end line, end column) in game.py)."""
    tree = parse(source)
    compiler = _Compiler(source, project or {})
    try:
        game_ir = compiler.program(tree)
    except RecursionError:
        raise PythonError("too_deep", say("py", "game.py is nested too deeply to translate")) from None
    return game_ir, compiler.origins


def python_to_ir(source: Any, project: dict[str, Any] | None = None) -> dict[str, Any]:
    return python_to_ir_traced(source, project)[0]


def check(source: Any, project: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    """The problems in game.py, for the IDE: [] when it compiles."""
    try:
        python_to_ir(source, project)
    except PythonError as exc:
        return [exc.as_problem()]
    return []


STARTER = '''"""My PRG32 game, in PRG32 Python."""
import prg32

player_x: int = 150
player_y: int = 180
score: int = 0


def init() -> None:
    """Runs once, when the game starts."""
    global score
    score = 0


def update() -> None:
    """Runs every frame: read the buttons, move things."""
    global player_x
    if prg32.held(prg32.LEFT):
        player_x -= 3
    if prg32.held(prg32.RIGHT):
        player_x += 3
    player_x = prg32.clamp(player_x, 0, 304)


def draw() -> None:
    """Runs every frame after update: draw the screen."""
    prg32.clear(prg32.BLACK)
    prg32.rect(player_x, player_y, 16, 16, prg32.YELLOW)
    prg32.text(8, 8, "PRG32", prg32.GREEN)
'''
