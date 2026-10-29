"""Blocks JSON -> validated game IR -> deterministic PRG32 C.

The pipeline has two small steps that students can inspect:

1. ``blocks_to_ir`` reads the saved Blockly workspace and produces a game IR:
   variables, lists, and three statement lists (init, update, draw). Every
   text field is checked by ``expressions.py`` before it enters the IR.
2. ``ir_to_c`` writes freestanding C for the PRG32 portable cartridge ABI.
   Kit helpers (lists, numbers on screen, lines, circles, bitmaps, math) are
   written into the same C file, and only when the game uses them.

``static/js/simulator.js`` reads the same blocks into the same IR so the
browser simulator and the generated C behave alike.
"""

# Shared with PRG32-Construction-Kit: prg32_construction_kit/generator.py at revision 0b9f1c0.
# Keep the two copies identical apart from the lines marked "SDK:"; see
# docs/software/python_language.md#keeping-the-sdk-and-the-kit-aligned.

from __future__ import annotations

import json
import re
import textwrap
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Any

from .expressions import (
    CONSTANTS,
    RESERVED_NAMES,  # noqa: F401  (re-exported for older imports)
    ExpressionError,
    c_identifier,
    check_expr,  # noqa: F401  (re-exported for tests and docs)
    expr_to_c,
    parse_expr,
    render,
    state_name,
)

IR_ABI = "prg32-construction-kit-game-1.1"
# SDK: the name written in the first comment of generated files (the kit writes "PRG32-Construction-Kit").
GENERATED_BY = "PRG32 SDK (python3 -m prg32 python)"

COLOR_NAMES = ["BLACK", "WHITE", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA", "ORANGE", "PINK",
               "GRAY", "DARK_GRAY", "BROWN", "PURPLE", "NAVY", "LIME"]
COLOR_MAP = {name: f"PRG32_COLOR_{name}" for name in COLOR_NAMES}
COLOR_DEFINES = {macro: f"0x{CONSTANTS[macro]:04X}" for macro in COLOR_MAP.values()}

BUTTON_NAMES = ["LEFT", "RIGHT", "UP", "DOWN", "A", "B", "START", "SELECT"]
BUTTON_MAP = {name: f"PRG32_BTN_{name}" for name in BUTTON_NAMES}
BUTTON_MAP.update({f"P2_{name}": f"PRG32_P2_BTN_{name}" for name in BUTTON_NAMES})

MAX_LIST_SIZE = 1024
MAX_REPEAT = 1024
SINE_TABLE = [0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49, 53, 58, 62, 66, 71, 75, 79, 83, 88, 92, 96, 100,
              104, 108, 112, 116, 120, 124, 128, 132, 136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171,
              175, 178, 181, 184, 187, 190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224,
              226, 228, 230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251, 252,
              253, 254, 254, 255, 255, 255, 256, 256, 256, 256]
_ON_PIXEL = re.compile(r"[^.\s0_-]")


def as_dict(value: Any) -> dict[str, Any]:
    if isinstance(value, str):
        if not value.strip():
            return {}
        value = json.loads(value)
    if isinstance(value, dict):
        return value
    return {}


def c_string(value: Any) -> str:
    """Escape text for a C string literal (quotes, backslashes, control characters, trigraphs)."""
    out = []
    for ch in str(value if value is not None else ""):
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 32 or ord(ch) == 127:
            out.append(f"\\{ord(ch):03o}")
        else:
            out.append(ch)
    text = "".join(out)
    while "??" in text:  # keep C trigraphs such as ??/ from changing the string
        text = text.replace("??", "?\\?")
    return text


def c_comment(value: Any) -> str:
    """Make text safe inside a /* ... */ comment: it can never close the comment early."""
    text = "".join(ch if 32 <= ord(ch) != 127 else " " for ch in str(value if value is not None else ""))
    while "*/" in text or "/*" in text:
        text = text.replace("*/", "* /").replace("/*", "/ *")
    return text.replace("??", "? ?")


def c_expr(value: Any, fallback: str = "0") -> str:
    text = str(value if value not in (None, "") else fallback)
    return check_expr(text) or check_expr(fallback) or "0"


_DECIMAL_RE = re.compile(r"[+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?")
_WHOLE_NUMBER_RE = re.compile(r"(-?)\s*(0[xX][0-9A-Fa-f]+|[0-9]+)")


def c_int(value: Any, fallback: int = 0) -> int:
    """Read a plain decimal number such as 10 or 2.5 (cut to 10 and 2); anything else gives the fallback.

    simulator.js ``cInt`` accepts exactly the same text.
    """
    text = str(value).strip()
    if not _DECIMAL_RE.fullmatch(text):
        return fallback
    try:
        return int(float(text))
    except (OverflowError, ValueError):
        return fallback


def int32(value: int) -> int:
    """Wrap a whole number to a 32-bit C int, like `value | 0` in JavaScript."""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def initial_value(value: Any) -> int:
    """The starting value of a variable: a plain whole number (like -3 or 0x10), otherwise 0."""
    match = _WHOLE_NUMBER_RE.fullmatch(str(value if value is not None else "").strip())
    if not match:
        return 0
    digits = match.group(2)
    number = int(digits, 16) if digits[:2].lower() == "0x" else int(digits, 10)
    if number > 0x7FFFFFFF:
        return 0
    return -number if match.group(1) else number


def color_name(value: Any, fallback: str = "WHITE") -> str:
    key = str(value or fallback).upper().strip()
    if key.startswith("PRG32_COLOR_"):
        key = key[len("PRG32_COLOR_"):]
    return key if key in COLOR_MAP else fallback


def color_macro(value: Any) -> str:
    return COLOR_MAP[color_name(value)]


def button_macro(value: Any) -> str:
    key = str(value or "A").upper().strip()
    if key in BUTTON_MAP.values():
        return key
    return BUTTON_MAP.get(key, "PRG32_BTN_A")


def bitmap_rows(block: dict[str, Any]) -> list[int]:
    """Read the eight ROW0..ROW7 fields: '.' is an empty pixel, anything else is lit."""
    rows = []
    for index in range(8):
        text = str(field_value(block, f"ROW{index}", "........"))[:8].ljust(8, ".")
        value = 0
        for col, ch in enumerate(text):
            if _ON_PIXEL.match(ch):
                value |= 0x80 >> col
        rows.append(value)
    return rows


def workspace_blocks(blocks_json: Any) -> list[dict[str, Any]]:
    data = as_dict(blocks_json)
    if "blocks" in data and isinstance(data["blocks"], dict):
        blocks = data["blocks"].get("blocks", [])
        return blocks if isinstance(blocks, list) else []
    if "blocks" in data and isinstance(data["blocks"], list):
        return data["blocks"]
    if "type" in data:
        return [data]
    return []


def field_value(block: dict[str, Any], name: str, fallback: Any = "") -> Any:
    fields = block.get("fields") or {}
    value = fields.get(name, fallback)
    if isinstance(value, dict):
        return value.get("value", fallback)
    return value


def input_block(block: dict[str, Any], name: str) -> dict[str, Any] | None:
    inputs = block.get("inputs") or {}
    candidate = inputs.get(name)
    if candidate is None and name == "DO":
        candidate = inputs.get("STACK")
    if isinstance(candidate, dict):
        child = candidate.get("block") or candidate.get("shadow")
        return child if isinstance(child, dict) else None
    return None


def next_block(block: dict[str, Any]) -> dict[str, Any] | None:
    nxt = block.get("next") or {}
    if isinstance(nxt, dict):
        child = nxt.get("block")
        return child if isinstance(child, dict) else None
    return None


def walk_stack(block: dict[str, Any] | None) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    seen: set[int] = set()
    while isinstance(block, dict) and id(block) not in seen:
        seen.add(id(block))
        out.append(block)
        block = next_block(block)
    return out


def all_blocks(blocks: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Every block in the workspace, including blocks nested in inputs."""
    out: list[dict[str, Any]] = []
    todo = list(blocks)
    seen: set[int] = set()
    while todo:
        block = todo.pop()
        if not isinstance(block, dict) or id(block) in seen:
            continue
        seen.add(id(block))
        out.append(block)
        nxt = next_block(block)
        if nxt:
            todo.append(nxt)
        for value in (block.get("inputs") or {}).values():
            if isinstance(value, dict):
                todo.extend(child for child in (value.get("block"), value.get("shadow")) if isinstance(child, dict))
    return out


@dataclass
class ParseContext:
    variables: dict[str, int] = field(default_factory=dict)
    lists: dict[str, int] = field(default_factory=dict)
    features: set[str] = field(default_factory=set)
    warnings: list[str] = field(default_factory=list)
    # id(IR statement) -> the Blockly block id it was made from, for the source maps
    # (source_maps.py, ADR-0015). It never changes the IR itself.
    origins: dict[int, str] = field(default_factory=dict)
    # The project's AudioStudio document (audio_studio.py): sound effects and soundtracks by name.
    audio: dict[str, Any] | None = None

    def remember(self, stmt: dict[str, Any], block: dict[str, Any]) -> None:
        if isinstance(block.get("id"), str) and block["id"]:
            self.origins[id(stmt)] = block["id"]

    def ensure_var(self, raw_name: Any, initial: int = 0) -> str | None:
        name = state_name(raw_name)
        if name in self.lists:
            self.warnings.append(f"{name} is a list; use the list blocks to change it")
            return None
        self.variables.setdefault(name, initial)
        return name

    def ensure_list(self, raw_name: Any) -> str | None:
        name = state_name(raw_name)
        if name not in self.lists:
            self.warnings.append(f"List {name} is used but never made with a 'make list' block")
            return None
        return name

    def expr(self, value: Any, fallback: Any = 0) -> str:
        """Validate one block field as an expression, warning when it is replaced."""
        if value in (None, ""):
            value = fallback
        try:
            parsed = parse_expr(value, self.lists)
        except (ExpressionError, RecursionError):
            self.warnings.append(f"Replaced unsupported expression {str(value)[:40]!r} with {fallback}")
            parsed = parse_expr(c_expr(fallback), self.lists)
        for name in sorted(parsed.names):
            self.variables.setdefault(name, 0)
        self.features |= parsed.features
        return render(parsed.tree)


def _field_expr(ctx: ParseContext, block: dict[str, Any], name: str, fallback: Any) -> str:
    return ctx.expr(field_value(block, name, fallback), fallback)


def _text(block: dict[str, Any], name: str, fallback: str, limit: int = 64) -> str:
    """Text for the PRG32 8x8 font, which only has printable ASCII: other characters become '?'."""
    raw = str(field_value(block, name, fallback))[:limit]
    return "".join(ch if 32 <= ord(ch) <= 126 else "?" for ch in raw)


def parse_statement(block: dict[str, Any], ctx: ParseContext) -> dict[str, Any] | None:
    btype = block.get("type")
    f = lambda name, fallback: _field_expr(ctx, block, name, fallback)  # noqa: E731
    if btype in {"prg32_set_state", "prg32_change_state", "prg32_clamp_state", "prg32_random_state", "prg32_wrap_state"}:
        initial = initial_value(field_value(block, "VALUE", 0)) if btype == "prg32_set_state" else 0
        name = ctx.ensure_var(field_value(block, "VAR", "score"), initial)
        if name is None:
            return {"op": "comment", "text": f"skipped: {btype} cannot change a list"}
        if btype == "prg32_set_state":
            return {"op": "set", "var": name, "value": f("VALUE", 0)}
        if btype == "prg32_change_state":
            return {"op": "change", "var": name, "delta": f("DELTA", 1)}
        op = {"prg32_clamp_state": "clamp", "prg32_random_state": "random", "prg32_wrap_state": "wrap"}[btype]
        high = 304 if op == "random" else 319 if op == "wrap" else 320
        return {"op": op, "var": name, "low": f("LOW", 0), "high": f("HIGH", high)}
    if btype in {"prg32_if", "prg32_if_else"}:
        stmt = {"op": "if", "cond": f("COND", 0), "then": parse_stack(input_block(block, "DO"), ctx)}
        if btype == "prg32_if_else":
            stmt["else"] = parse_stack(input_block(block, "ELSE"), ctx)
        return stmt
    if btype == "prg32_repeat":
        name = ctx.ensure_var(field_value(block, "VAR", "i"))
        if name is None:
            return {"op": "comment", "text": "skipped: a repeat block cannot count with a list"}
        return {"op": "repeat", "var": name, "from": f("FROM", 0), "to": f("TO", 9),
                "body": parse_stack(input_block(block, "DO"), ctx)}
    if btype == "prg32_list_create":
        name = state_name(field_value(block, "LIST", "items"))
        return {"op": "list_fill", "list": name, "value": f("VALUE", 0)}
    if btype in {"prg32_list_set", "prg32_list_change"}:
        name = ctx.ensure_list(field_value(block, "LIST", "items"))
        if name is None:
            return {"op": "comment", "text": "skipped: unknown list"}
        if btype == "prg32_list_set":
            return {"op": "list_set", "list": name, "index": f("INDEX", 0), "value": f("VALUE", 0)}
        return {"op": "list_change", "list": name, "index": f("INDEX", 0), "delta": f("DELTA", 1)}
    if btype in {"prg32_if_button", "prg32_if_button_once"}:
        button = str(field_value(block, "BUTTON", "A")).upper()
        if button not in BUTTON_MAP:
            button = "A"
        return {"op": "if_button" if btype == "prg32_if_button" else "if_button_once", "button": button,
                "then": parse_stack(input_block(block, "DO"), ctx)}
    if btype == "prg32_if_touching":
        ctx.features.add("sprites")
        stmt = {"op": "if_touching"}
        for key, fallback in (("AX", "player_x"), ("AY", "player_y"), ("AW", 16), ("AH", 16),
                              ("BX", "enemy_x"), ("BY", "enemy_y"), ("BW", 16), ("BH", 16)):
            stmt[key.lower()] = f(key, fallback)
        stmt["then"] = parse_stack(input_block(block, "DO"), ctx)
        return stmt
    if btype == "prg32_clear_screen":
        return {"op": "clear", "color": color_name(field_value(block, "COLOR", "BLACK"), "BLACK")}
    if btype in {"prg32_draw_rect", "prg32_draw_frame"}:
        return {"op": "rect" if btype == "prg32_draw_rect" else "frame", "x": f("X", 0), "y": f("Y", 0),
                "w": f("W", 16), "h": f("H", 16), "color": color_name(field_value(block, "COLOR", "WHITE"))}
    if btype == "prg32_draw_rect_rgb":
        return {"op": "rect_rgb", "x": f("X", 0), "y": f("Y", 0), "w": f("W", 16), "h": f("H", 16),
                "rgb": f("RGB", "0xFFFF")}
    if btype == "prg32_draw_pixel":
        return {"op": "pixel", "x": f("X", 0), "y": f("Y", 0), "color": color_name(field_value(block, "COLOR", "WHITE"))}
    if btype == "prg32_draw_line":
        return {"op": "line", "x1": f("X1", 0), "y1": f("Y1", 0), "x2": f("X2", 16), "y2": f("Y2", 16),
                "color": color_name(field_value(block, "COLOR", "WHITE"))}
    if btype == "prg32_draw_circle":
        return {"op": "circle", "x": f("X", 160), "y": f("Y", 100), "r": f("R", 8),
                "color": color_name(field_value(block, "COLOR", "WHITE")),
                "fill": str(field_value(block, "FILL", "OUTLINE")).upper() == "FILLED"}
    if btype == "prg32_draw_bitmap":
        return {"op": "bitmap", "rows": bitmap_rows(block), "x": f("X", 0), "y": f("Y", 0),
                "color": color_name(field_value(block, "COLOR", "WHITE")), "scale": f("SCALE", 1)}
    if btype == "prg32_draw_sprite8":
        ctx.features.add("sprites")
        return {"op": "sprite8", "rows": bitmap_rows(block), "x": f("X", 0), "y": f("Y", 0),
                "fg": color_name(field_value(block, "FG", "WHITE")), "bg": color_name(field_value(block, "BG", "BLACK"), "BLACK")}
    if btype in {"prg32_draw_pixel_indexed", "prg32_draw_rect_indexed"}:
        result = {"op": "pixel_indexed" if btype == "prg32_draw_pixel_indexed" else "rect_indexed",
                  "x": f("X", 0), "y": f("Y", 0), "index": f("INDEX", 1)}
        if btype == "prg32_draw_rect_indexed":
            result["w"] = f("W", 16)
            result["h"] = f("H", 16)
        return result
    if btype == "prg32_palette_set":
        return {"op": "palette_set", "index": f("INDEX", 1), "color": color_name(field_value(block, "COLOR", "WHITE"))}
    if btype == "prg32_clear_indexed":
        return {"op": "clear_indexed", "index": f("INDEX", 0)}
    if btype == "prg32_draw_text":
        return {"op": "text", "x": f("X", 0), "y": f("Y", 0), "text": _text(block, "TEXT", "HELLO"),
                "fg": color_name(field_value(block, "FG", "WHITE")), "bg": color_name(field_value(block, "BG", "BLACK"), "BLACK")}
    if btype == "prg32_draw_number":
        return {"op": "number", "value": f("VALUE", "score"), "x": f("X", 0), "y": f("Y", 0),
                "fg": color_name(field_value(block, "FG", "WHITE")), "bg": color_name(field_value(block, "BG", "BLACK"), "BLACK")}
    if btype == "prg32_tile_define":
        ctx.features.add("tilemap")
        return {"op": "tile_define", "id": f("ID", 1), "rows": bitmap_rows(block),
                "fg": color_name(field_value(block, "FG", "WHITE")), "bg": color_name(field_value(block, "BG", "BLACK"), "BLACK")}
    if btype == "prg32_playfield_clear":
        ctx.features.add("tilemap")
        return {"op": "playfield_clear", "layer": f("LAYER", 0), "id": f("ID", 0)}
    if btype == "prg32_playfield_put":
        ctx.features.add("tilemap")
        return {"op": "playfield_put", "layer": f("LAYER", 0), "tx": f("TX", 0), "ty": f("TY", 0), "id": f("ID", 1)}
    if btype == "prg32_playfield_scroll":
        ctx.features.add("tilemap")
        return {"op": "playfield_scroll", "layer": f("LAYER", 0), "x": f("X", 0), "y": f("Y", 0)}
    if btype == "prg32_playfield_camera":
        ctx.features.add("tilemap")
        return {"op": "playfield_camera", "x": f("X", 0), "y": f("Y", 0)}
    if btype == "prg32_playfield_draw":
        ctx.features.add("tilemap")
        return {"op": "playfield_draw", "layer": f("LAYER", 0),
                "transparent": str(field_value(block, "TRANSPARENT", "OPAQUE")).upper() == "TRANSPARENT"}
    if btype == "prg32_play_beep":
        ctx.features.add("audio")
        return {"op": "beep", "freq": f("FREQ", 880), "ms": f("MS", 80)}
    if btype == "prg32_audio_note":
        ctx.features.add("audio")
        return {"op": "note", "note": f("NOTE", 60), "channel": f("CHANNEL", 0), "ms": f("MS", 250)}
    if btype == "prg32_audio_stop":
        ctx.features.add("audio")
        return {"op": "note_off", "channel": f("CHANNEL", 0)}
    if btype == "prg32_audio_volume":
        ctx.features.add("audio")
        return {"op": "volume", "volume": f("VOLUME", 200)}
    if btype in ("prg32_sound_effect", "prg32_soundtrack_play"):
        # AudioStudio (docs/audio_studio.md): the name becomes fixed numbers from the project's
        # document, so the generated code says exactly which instrument, voice and track play.
        ctx.features.add("audio")
        audio = ctx.audio or {"effects": [], "soundtracks": []}
        if btype == "prg32_sound_effect":
            name = str(field_value(block, "EFFECT", ""))[:32]
            index = next((i for i, item in enumerate(audio.get("effects", [])) if item["name"] == name), None)
            if index is None:
                ctx.warnings.append(f"The sound effect {name!r} is not in this project's AudioStudio")
                return {"op": "comment", "text": f"skipped: no sound effect named {name}"}
            effect = audio["effects"][index]
            return {"op": "sfx", "name": name, "voice": effect["voice"], "instrument": 8 + index,
                    "note": effect["note"], "volume": effect["volume"], "ms": effect["duration_ms"]}
        name = str(field_value(block, "TRACK", ""))[:32]
        index = next((i for i, item in enumerate(audio.get("soundtracks", [])) if item["name"] == name), None)
        if index is None:
            ctx.warnings.append(f"The soundtrack {name!r} is not in this project's AudioStudio")
            return {"op": "comment", "text": f"skipped: no soundtrack named {name}"}
        return {"op": "track_play", "name": name, "track": index}
    if btype == "prg32_soundtrack_stop":
        ctx.features.add("audio")
        return {"op": "track_stop"}
    if btype == "prg32_sound_stop_all":
        ctx.features.add("audio")
        return {"op": "sound_stop_all"}
    if btype == "prg32_band_text":
        value = field_value(block, "VALUE", "")
        stmt = {"op": "band", "text": _text(block, "TEXT", "SCORE", 24)}
        if str(value).strip():
            stmt["value"] = ctx.expr(value, 0)
        return stmt
    if btype == "prg32_score_submit":
        return {"op": "score_submit", "value": f("VALUE", "score")}
    if btype == "prg32_scoreboard_show":
        return {"op": "scoreboard", "title": _text(block, "TITLE", "HIGH SCORES", 32)}
    if btype == "prg32_splash":
        return {"op": "splash", "title": _text(block, "TITLE", "MY GAME", 32), "subtitle": _text(block, "SUBTITLE", "PRESS START", 32),
                "ms": f("MS", 1500), "bg": color_name(field_value(block, "BG", "BLACK"), "BLACK"),
                "fg": color_name(field_value(block, "FG", "WHITE")), "accent": color_name(field_value(block, "ACCENT", "YELLOW"))}
    if btype == "prg32_player_name":
        return {"op": "player_name"}
    if btype == "prg32_mp_join":
        ctx.features.add("multiplayer")
        return {"op": "mp_join"}
    if btype == "prg32_mp_share":
        ctx.features.add("multiplayer")
        return {"op": "mp_share", "x": f("X", "player_x"), "y": f("Y", "player_y"), "look": f("LOOK", 0)}
    if btype == "prg32_comment":
        return {"op": "comment", "text": str(field_value(block, "TEXT", "comment"))}
    ctx.warnings.append(f"Skipped unsupported block type: {str(btype)[:40]}")
    return {"op": "comment", "text": f"Unsupported block {str(btype)[:40]}"}


def parse_stack(first_block: dict[str, Any] | None, ctx: ParseContext) -> list[dict[str, Any]]:
    statements: list[dict[str, Any]] = []
    for block in walk_stack(first_block):
        stmt = parse_statement(block, ctx)
        if stmt:
            ctx.remember(stmt, block)
            statements.append(stmt)
    return statements


def blocks_to_ir(blocks_json: Any, project: dict[str, Any] | None = None) -> dict[str, Any]:
    return blocks_to_ir_traced(blocks_json, project)[0]


def blocks_to_ir_traced(blocks_json: Any, project: dict[str, Any] | None = None) -> tuple[dict[str, Any], dict[int, str]]:
    """blocks_to_ir, plus which block made each IR statement (keyed by id() of the statement)."""
    project = project or {}
    ctx = ParseContext(audio=project.get("audio_studio") or None)
    init: list[dict[str, Any]] = []
    update: list[dict[str, Any]] = []
    draw: list[dict[str, Any]] = []
    top_blocks = workspace_blocks(blocks_json)

    # Lists are found first, so any block may read a list made in 'when game starts'.
    for block in all_blocks(top_blocks):
        if block.get("type") == "prg32_list_create":
            name = state_name(field_value(block, "LIST", "items"))
            size = max(1, min(MAX_LIST_SIZE, c_int(field_value(block, "SIZE", 10), 10)))
            if name in ctx.lists and ctx.lists[name] != size:
                ctx.warnings.append(f"List {name} is made twice with different sizes; using the larger one")
            ctx.lists[name] = max(size, ctx.lists.get(name, 0))

    for top in top_blocks:
        btype = top.get("type")
        if btype == "prg32_on_start":
            init.extend(parse_stack(input_block(top, "DO"), ctx))
        elif btype == "prg32_update":
            update.extend(parse_stack(input_block(top, "DO"), ctx))
        elif btype == "prg32_draw":
            draw.extend(parse_stack(input_block(top, "DO"), ctx))
        elif btype:
            # Loose blocks are treated as update logic so student work is not lost.
            stmt = parse_statement(top, ctx)
            if stmt:
                ctx.remember(stmt, top)
                update.append(stmt)

    if not ctx.variables and not ctx.lists:
        ctx.variables.update({"player_x": 150, "player_y": 180, "score": 0})
    if not draw:
        ctx.variables.setdefault("player_x", 150)
        ctx.variables.setdefault("player_y", 180)
        draw = [
            {"op": "clear", "color": "BLACK"},
            {"op": "rect", "x": "player_x", "y": "player_y", "w": "16", "h": "16", "color": "YELLOW"},
            {"op": "text", "x": "8", "y": "8", "text": "PRG32", "fg": "GREEN", "bg": "BLACK"},
        ]

    title = project.get("title") or "PRG32 Game"
    prefix = c_identifier(project.get("entry_prefix") or title, "prg32_game")
    return {
        "abi": IR_ABI,
        "title": title,
        "description": project.get("description", ""),
        "author": project.get("author", "Student"),
        "entry_prefix": prefix,
        "state": [{"name": name, "type": "int", "initial": initial} for name, initial in ctx.variables.items()],
        "lists": [{"name": name, "size": size} for name, size in ctx.lists.items()],
        "required_features": sorted(ctx.features),
        "init": init,
        "update": update,
        "draw": draw,
        "warnings": ctx.warnings,
    }, ctx.origins


# ---------------------------------------------------------------------------
# Plain-language narration, shared by the C and the assembly generators
# ---------------------------------------------------------------------------

_SIMPLE_VALUE = re.compile(r"-?(?:0[xX][0-9A-Fa-f]+|[0-9]+)|[A-Za-z_][A-Za-z0-9_]*")


def _colour_words(value: Any, fallback: str = "WHITE") -> str:
    return color_name(value, fallback).replace("_", " ").lower()


def _button_words(value: Any) -> str:
    key = str(value or "A").upper().strip()
    if key not in BUTTON_MAP:
        key = "A"
    return key.replace("P2_", "player 2 ")


def narrate(sentence: str, python: bool) -> str:
    """A PRG32 Python project has statements, not blocks: 'Block "x"' reads 'Step "x"' there."""
    return "Step " + sentence[len("Block "):] if python and sentence.startswith('Block "') else sentence


def is_python(game_ir: dict[str, Any]) -> bool:
    """True for the IR of a PRG32 Python project (python_project.py, ADR-0026)."""
    return game_ir.get("source_mode") == "python"


def describe_statement(stmt: dict[str, Any], show: Callable[[Any], str] | None = None) -> str:
    """One plain sentence that says what a block does.

    The C and the assembly generators both print this sentence above the code
    of every block, so a student can match a block, its C lines and its
    RISC-V instructions. ``show`` turns an IR expression into the checked text
    to print (by default it is checked without lists).
    """
    show = show or (lambda text: check_expr(text) or "0")
    op = stmt.get("op")

    def v(key: str, fallback: Any = 0) -> str:
        value = stmt.get(key)
        return show(value if value not in (None, "") else fallback)

    var = state_name(stmt.get("var")) if "var" in stmt else ""
    lst = state_name(stmt.get("list")) if "list" in stmt else ""
    colour = _colour_words(stmt.get("color"))
    fg, bg = _colour_words(stmt.get("fg")), _colour_words(stmt.get("bg"), "BLACK")
    if op == "set":
        value = v("value")
        if _SIMPLE_VALUE.fullmatch(value):
            return f'Block "set {var} to {value}": store {value} in the variable {var}.'
        return f'Block "set {var} to {value}": work out {value}, then store the result in {var}.'
    if op == "change":
        delta = v("delta")
        if delta.startswith("-"):
            return f'Block "change {var} by {delta}": add {delta} to {var}; adding a negative number subtracts.'
        return f'Block "change {var} by {delta}": add {delta} to {var}.'
    if op == "clamp":
        return (f'Block "keep {var} between {v("low")} and {v("high")}": raise {var} to {v("low")} when it is smaller, '
                f'lower it to {v("high")} when it is larger.')
    if op == "wrap":
        return (f'Block "wrap {var} from {v("low")} to {v("high")}": a value that leaves one end of the range comes back '
                f'in at the other end, like a tunnel; the range holds ({v("high")}) - ({v("low")}) + 1 values.')
    if op == "random":
        return f'Block "set {var} to a random number": pick a whole number from {v("low")} to {v("high")}, both ends included.'
    if op == "if":
        text = f'Block "if {v("cond")}": the inner blocks run only when the condition is true (not 0)'
        return text + ('; otherwise the "else" blocks run.' if "else" in stmt else ".")
    if op == "repeat":
        return (f'Block "repeat {var} from {v("from")} to {v("to")}": run the inner blocks once for every value of {var}, '
                f'counting up by 1; a hidden counter stops after {MAX_REPEAT} rounds, so a frame always ends.')
    if op == "list_fill":
        return f'Block "make list {lst}": give every item of {lst} the value {v("value")}.'
    if op == "list_set":
        return (f'Block "set {lst}[{v("index")}] to {v("value")}": a position outside the list is ignored, '
                f'so a wrong position can never overwrite other memory.')
    if op == "list_change":
        return (f'Block "change {lst}[{v("index")}] by {v("delta")}": read the item, add {v("delta")}, write it back '
                f'(outside the list an item reads as 0 and nothing is written).')
    if op == "if_button":
        return f'Block "if button {_button_words(stmt.get("button"))} is held": true on every frame while the button is down.'
    if op == "if_button_once":
        return (f'Block "if button {_button_words(stmt.get("button"))} was just pressed": true only on the first frame '
                f'of a press (down now, up one frame ago).')
    if op == "if_touching":
        return (f'Block "if touching": run the inner blocks when box A (x {v("ax")}, y {v("ay")}, {v("aw")} by {v("ah")}) '
                f'overlaps box B (x {v("bx")}, y {v("by")}, {v("bw")} by {v("bh")}).')
    if op == "clear":
        return f'Block "clear screen": paint the whole 320 x 200 game area {_colour_words(stmt.get("color"), "BLACK")}.'
    if op == "rect":
        return f'Block "draw rectangle": a filled {v("w")} x {v("h")} rectangle, top-left corner at ({v("x")}, {v("y")}), {colour}.'
    if op == "rect_rgb":
        return (f'Block "draw rectangle (RGB565)": a filled {v("w")} x {v("h")} rectangle at ({v("x")}, {v("y")}) '
                f'whose colour is the number {v("rgb")}.')
    if op == "frame":
        return f'Block "draw frame": the 1-pixel outline of a {v("w")} x {v("h")} rectangle at ({v("x")}, {v("y")}), {colour}.'
    if op == "pixel":
        return f'Block "draw pixel": light the single pixel at ({v("x")}, {v("y")}) in {colour}.'
    if op == "line":
        return f'Block "draw line": a straight line from ({v("x1")}, {v("y1")}) to ({v("x2")}, {v("y2")}), {colour}.'
    if op == "circle":
        kind = "filled" if stmt.get("fill") else "outlined"
        return f'Block "draw circle": a {kind} circle with centre ({v("x")}, {v("y")}) and radius {v("r")}, {colour}.'
    if op == "bitmap":
        return (f'Block "draw 8x8 picture": top-left corner at ({v("x")}, {v("y")}); every picture pixel becomes a '
                f'{v("scale")} x {v("scale")} square of screen pixels, {colour}.')
    if op == "sprite8":
        return f'Block "draw 8x8 sprite": the firmware draws the picture at ({v("x")}, {v("y")}), lit pixels {fg}, the others {bg}.'
    if op == "palette_set":
        return f'Block "set palette colour": palette entry {v("index")} becomes {colour}; indexed drawing looks colours up here.'
    if op == "clear_indexed":
        return f'Block "clear screen with palette colour {v("index")}".'
    if op == "rect_indexed":
        return f'Block "draw rectangle with palette colour {v("index")}": {v("w")} x {v("h")} at ({v("x")}, {v("y")}).'
    if op == "pixel_indexed":
        return f'Block "draw pixel with palette colour {v("index")}" at ({v("x")}, {v("y")}).'
    if op == "text":
        return f'Block "draw text": write "{stmt.get("text", "")}" with the 8x8 font at ({v("x")}, {v("y")}), {fg} on {bg}.'
    if op == "number":
        return f'Block "draw number": write the value of {v("value")} in decimal digits at ({v("x")}, {v("y")}), {fg} on {bg}.'
    if op == "tile_define":
        return f'Block "define tile {v("id")}": store an 8x8 picture as tile number {v("id")}, {fg} on {bg}, for the playfields.'
    if op == "playfield_clear":
        return f'Block "clear playfield {v("layer")}": fill every cell of layer {v("layer")} with tile {v("id")}.'
    if op == "playfield_put":
        return f'Block "put tile": tile {v("id")} goes to column {v("tx")}, row {v("ty")} of playfield layer {v("layer")}.'
    if op == "playfield_scroll":
        return f'Block "scroll playfield": layer {v("layer")} shows its pixel ({v("x")}, {v("y")}) at the top-left of the screen.'
    if op == "playfield_camera":
        return f'Block "move camera": the camera looks at ({v("x")}, {v("y")}) of the playfields.'
    if op == "playfield_draw":
        how = "tile 0 is see-through" if stmt.get("transparent") else "every tile is solid"
        return f'Block "draw playfield {v("layer")}": copy the visible tiles of layer {v("layer")} to the screen ({how}).'
    if op == "beep":
        return f'Block "beep": play the musical note nearest to {v("freq")} Hz for {v("ms")} milliseconds on channel 0.'
    if op == "note":
        return f'Block "play note": MIDI note {v("note")} (60 is middle C) on channel {v("channel")} for {v("ms")} milliseconds.'
    if op == "note_off":
        return f'Block "stop sound": silence channel {v("channel")}.'
    if op == "volume":
        return f'Block "set volume": master volume {v("volume")} (0 is silent, 255 is loudest).'
    if op == "sfx":
        return (f'Block "play sound effect {stmt.get("name")}": AudioStudio instrument {stmt.get("instrument")} plays '
                f'note {stmt.get("note")} on voice {stmt.get("voice")} for {stmt.get("ms")} ms, then its envelope '
                'releases.')
    if op == "track_play":
        return (f'Block "play soundtrack {stmt.get("name")}": track {stmt.get("track")} of the cartridge\'s audio '
                'block starts; voice N plays instrument N.')
    if op == "track_stop":
        return 'Block "stop soundtrack": the soundtrack stops (notes already playing fade out).'
    if op == "sound_stop_all":
        return 'Block "stop all sounds": every voice goes silent at once.'
    if op == "band":
        shown = f' followed by the value of {v("value")}' if "value" in stmt else ""
        return f'Block "status band": show "{stmt.get("text", "")}"{shown} in the firmware status band.'
    if op == "score_submit":
        return f'Block "submit score": record {v("value")} as the score of the current player of this game.'
    if op == "scoreboard":
        return f'Block "show high scores": the firmware shows the score table titled "{stmt.get("title", "")}".'
    if op == "splash":
        return (f'Block "splash screen": show "{stmt.get("title", "")}" and "{stmt.get("subtitle", "")}" '
                f'for {v("ms")} milliseconds.')
    if op == "player_name":
        return 'Block "ask player name": the firmware shows its keyboard so the player can type a name.'
    if op == "mp_join":
        return 'Block "join multiplayer": connect to other PRG32 players of this game through the Cartridge Store relay.'
    if op == "mp_share":
        return (f'Block "share my position": send x {v("x")}, y {v("y")} and look {v("look")} to the other players, '
                f'then exchange messages with the relay.')
    return ""


# ---------------------------------------------------------------------------
# C generation
# ---------------------------------------------------------------------------

# Trailing comments start at this column, so code and explanation read side by side.
C_COMMENT_COLUMN = 56
_C_WRAP = 100


# The first time each kind of statement appears in a file, the C line gets a note
# that explains its syntax (later uses rely on that first explanation).
C_SYNTAX_NOTES = {
    "set": "C syntax: '=' stores the value on the right in the variable on the left",
    "change": "C syntax: x += d is short for x = x + d",
    "clamp": "C syntax: if (test) { ... } runs the braces only when the test is true",
    "wrap": "C syntax: 'else if' is tested only when the test before it was false",
    "random": "a function call: the values in brackets are its inputs, in order",
    "if": "C syntax: if (condition) { ... }; any value except 0 counts as true",
    "repeat": "C syntax: { ... } groups statements; variables declared inside exist only there",
    "list_fill": "a list is passed by its address, so the helper can change its items",
    "list_set": "helpers check the position, so a list can never be written outside its items",
    "list_change": "the inner call reads the item, the outer call writes the new value back",
    "if_button": "C syntax: & (bitwise AND) keeps only this button's bit: not 0 exactly when it is held",
    "if_button_once": "C syntax: && means 'and', ! means 'not'; previous input holds last frame's buttons",
    "if_touching": "the firmware compares the two boxes and answers 1 (overlap) or 0",
    "clear": "a firmware call: the PRG32 firmware does the drawing",
    "text": "text in quotes \"...\" is a C string: its characters followed by a 0 byte",
    "number": "kit helper: turns the number into text, then draws it",
    "bitmap": "the picture is passed by its address: the name of an array is its address",
    "sprite8": "the picture is passed by its address: the name of an array is its address",
    "beep": "the inner call runs first; its result becomes the third input of the outer call",
    "band": "1 says 'a number follows'; 0 would show the text alone",
}


def c_comment_block(text: str, indent: str = "", width: int = _C_WRAP) -> list[str]:
    """A /* ... */ comment, wrapped to ``width`` columns."""
    body = textwrap.wrap(c_comment(text), max(30, width - len(indent) - 3)) or [""]
    if len(body) == 1:
        return [f"{indent}/* {body[0]} */"]
    return [f"{indent}/* {body[0]}", *(f"{indent} * {line}" for line in body[1:-1]), f"{indent} * {body[-1]} */"]


def annotate_c(rows: list[Any], column: int = C_COMMENT_COLUMN) -> list[str]:
    """Lay out (code, explanation) pairs: the explanation goes after the code, or above a long line.

    A plain string is copied as it is.
    """
    out: list[str] = []
    for row in rows:
        if isinstance(row, str):
            out.append(row)
            continue
        code, note = row
        if not note:
            out.append(code)
        elif len(code) + 2 <= column:
            out.append(code.ljust(column) + f"/* {c_comment(note)} */")
        else:
            indent = code[:len(code) - len(code.lstrip())]
            out.extend(c_comment_block(note, indent))
            out.append(code)
    return out


def _doc(*lines: str) -> list[str]:
    """A helper's documentation block."""
    return ["/*", *(f" * {line}".rstrip() for line in lines), " */"]


# Helper C functions, written into the game file only when the game needs them.
# Every helper has a documentation block (what, inputs, result, method) and an
# explanation beside every line. Kit helpers start with prg32_kit_ so they can
# never clash with a student's variable names (those may not start with prg32_).
_HELPER_SOURCE: dict[str, list[Any]] = {
    "list_get": [
        *_doc("prg32_kit_get(list, size, index): read one item of a list, safely.",
              "C never checks array positions: list[-1] or list[size] would quietly read",
              "memory that belongs to something else. This helper answers 0 instead.",
              "Inputs: the address of item 0, the number of items, the wanted position.",
              "Result: list[index] when 0 <= index < size, otherwise 0."),
        ("static int prg32_kit_get(const int *list, int size, int index) {", ""),
        ("    return (index >= 0 && index < size) ? list[index] : 0;", "inside the list: the item; outside: 0"),
        ("}", ""),
    ],
    "list_set": [
        *_doc("prg32_kit_set(list, size, index, value): write one item of a list, safely.",
              "Writing outside an array is the classic C bug (a 'buffer overflow'):",
              "it overwrites other variables. Here a write outside 0..size-1 does nothing."),
        ("static void prg32_kit_set(int *list, int size, int index, int value) {", ""),
        ("    if (index >= 0 && index < size) list[index] = value;", "only positions 0..size-1 are written"),
        ("}", ""),
    ],
    "list_fill": [
        *_doc("prg32_kit_fill(list, size, value): give every item of a list the same value.",
              "A counting loop: index walks 0, 1, ..., size-1."),
        ("static void prg32_kit_fill(int *list, int size, int value) {", ""),
        ("    int index;", "the position being written"),
        ("    for (index = 0; index < size; ++index) list[index] = value;", "start at 0, stop before size, step 1"),
        ("}", ""),
    ],
    "div": [
        *_doc("prg32_kit_div(a, b): whole-number division a / b, rounded toward zero (7 / 2 = 3).",
              "In C, dividing by 0 (or -2147483648 / -1) is 'undefined behaviour': anything may",
              "happen. The RISC-V DIV instruction instead defines every case: x / 0 = -1 and",
              "-2147483648 / -1 = -2147483648. This helper gives exactly the RISC-V answers,",
              "the same ones the browser simulator gives."),
        ("static int prg32_kit_div(int a, int b) {", ""),
        ("    if (b == 0) return -1;", "RISC-V rule: x / 0 = -1 (no crash)"),
        ("    if (b == -1) return (int)(0u - (unsigned int)a);", "a / -1 = -a, computed without overflow"),
        ("    return a / b;", "the normal case"),
        ("}", ""),
    ],
    "mod": [
        *_doc("prg32_kit_mod(a, b): the remainder of a / b; its sign follows a (-7 % 2 = -1).",
              "Like the RISC-V REM instruction: x % 0 = x and x % -1 = 0, never a crash."),
        ("static int prg32_kit_mod(int a, int b) {", ""),
        ("    if (b == 0) return a;", "RISC-V rule: x % 0 = x"),
        ("    if (b == -1) return 0;", "every number divides by -1 exactly"),
        ("    return a % b;", "the normal case"),
        ("}", ""),
    ],
    "abs": [
        *_doc("prg32_kit_abs(value): the distance of value from 0 (abs(-5) = 5)."),
        ("static int prg32_kit_abs(int value) { return value < 0 ? -value : value; }", "negative: flip the sign"),
    ],
    "sign": [
        *_doc("prg32_kit_sign(value): -1 for a negative value, 0 for zero, 1 for a positive value.",
              "A comparison in C is itself a number, 1 (true) or 0 (false), so",
              "(value > 0) - (value < 0) is 1 - 0, 0 - 0 or 0 - 1."),
        ("static int prg32_kit_sign(int value) { return (value > 0) - (value < 0); }", ""),
    ],
    "min": [
        *_doc("prg32_kit_min(a, b): the smaller of two numbers.",
              "condition ? x : y is C's 'conditional operator': x when the condition is true, otherwise y."),
        ("static int prg32_kit_min(int a, int b) { return a < b ? a : b; }", ""),
    ],
    "max": [
        *_doc("prg32_kit_max(a, b): the larger of two numbers."),
        ("static int prg32_kit_max(int a, int b) { return a > b ? a : b; }", ""),
    ],
    "clamp": [
        *_doc("prg32_kit_clamp(value, low, high): value, pushed back inside low..high."),
        ("static int prg32_kit_clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }", ""),
    ],
    "sin": [
        *_doc("prg32_kit_sin(degrees) and prg32_kit_cos(degrees): sine and cosine without floating point.",
              "The result is scaled by 256: 256 means 1.0 and -128 means -0.5, so",
              "x + speed * cos(angle) / 256 moves x by 'speed' pixels in the angle's direction.",
              "The table holds round(256 * sin(d)) for d = 0..90 degrees. The other three",
              "quarters of the circle are mirror images of the first one:",
              "  sin(d) = sin(180 - d),  sin(180 + d) = -sin(d),  sin(360 - d) = -sin(d)."),
        ("static const int16_t prg32_kit_sine_table[91] = {", "91 entries: one per degree, 0..90"),
        *("    " + ", ".join(str(v) for v in SINE_TABLE[i:i + 16]) + ("," if i + 16 < len(SINE_TABLE) else "")
          for i in range(0, len(SINE_TABLE), 16)),
        "};",
        ("static int prg32_kit_sin(int degrees) {", ""),
        ("    int a = degrees % 360;", "one full turn is 360 degrees; a is now -359..359"),
        ("    if (a < 0) a += 360;", "bring a into 0..359"),
        ("    if (a <= 90) return prg32_kit_sine_table[a];", "first quarter: read the table"),
        ("    if (a <= 180) return prg32_kit_sine_table[180 - a];", "second quarter: mirror around 90"),
        ("    if (a <= 270) return -prg32_kit_sine_table[a - 180];", "third quarter: same shape, negative"),
        ("    return -prg32_kit_sine_table[360 - a];", "fourth quarter: mirror and negative"),
        ("}", ""),
    ],
    "cos": [
        *_doc("prg32_kit_cos(degrees): the cosine is the sine 90 degrees later: cos(d) = sin(d + 90)."),
        ("static int prg32_kit_cos(int degrees) { return prg32_kit_sin(degrees % 360 + 90); }", ""),
    ],
    "rgb": [
        *_doc("prg32_kit_rgb(red, green, blue): pack three 0..255 values into one RGB565 colour.",
              "RGB565 keeps the top 5 bits of red, 6 of green (our eyes see green best) and 5 of blue:",
              "  bit 15..11 red | bit 10..5 green | bit 4..0 blue."),
        ("static int prg32_kit_rgb(int r, int g, int b) {", ""),
        ("    r = r < 0 ? 0 : r > 255 ? 255 : r;", "keep red inside 0..255"),
        ("    g = g < 0 ? 0 : g > 255 ? 255 : g;", "keep green inside 0..255"),
        ("    b = b < 0 ? 0 : b > 255 ? 255 : b;", "keep blue inside 0..255"),
        ("    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);",
         "& keeps the top bits, << and >> move them into place, | joins the three parts"),
        ("}", ""),
    ],
    "random": [
        *_doc("prg32_kit_random(low, high): a whole random number from low to high, both included.",
              "The firmware gives a random number from 0 to (high - low); adding low shifts the range."),
        ("static int prg32_kit_random(int low, int high) {", ""),
        ("    if (high <= low) return low;", "an empty or one-value range: the answer is low"),
        ("    return low + (int)prg32_random_number(0u, (uint32_t)(high - low));", "low + (0 .. high-low)"),
        ("}", ""),
    ],
    "frame": [
        *_doc("prg32_kit_frame(): how many frames have finished since the game started.",
              "At 30 frames per second, frame() / 30 counts seconds. The update function adds 1 per frame."),
        ("static int prg32_kit_frame_count;", "a static variable keeps its value between frames"),
        ("static int prg32_kit_frame(void) { return prg32_kit_frame_count; }", ""),
    ],
    "tile": [
        *_doc("prg32_kit_tile(layer, column, row): the tile number stored in one playfield cell.",
              "The firmware takes 8-bit numbers (0..255); anything outside that range answers 0",
              "instead of being cut down to 8 bits and reading the wrong cell."),
        ("static int prg32_kit_tile(int layer, int tx, int ty) {", ""),
        ("    if (layer < 0 || tx < 0 || ty < 0 || layer > 255 || tx > 255 || ty > 255) return 0;", "|| means 'or'"),
        ("    return (int)prg32_playfield_get((uint8_t)layer, (uint8_t)tx, (uint8_t)ty);", "ask the firmware"),
        ("}", ""),
    ],
    "peer_value": [
        *_doc("prg32_kit_peer_value(index, what): one value about another player on the Cartridge Store relay.",
              "what = 0 gives x, 1 gives y, 2 gives the 'look' (sprite) number. When the player",
              "does not exist (for example when playing alone) the answer is 0.",
              "The firmware fills a structure (a group of named values) that lives on the stack."),
        ("static int prg32_kit_peer_value(int index, int what) {", ""),
        ("    prg32_player_state_t peer;", "space for the other player's data"),
        ("    if (prg32_multiplayer_get_peer(index, &peer) != 0) return 0;", "&peer is the address the firmware fills"),
        ("    return what == 0 ? peer.x : what == 1 ? peer.y : peer.sprite;", "pick one field of the structure"),
        ("}", ""),
    ],
    "peers": [
        *_doc("prg32_kit_peers(): how many other players are connected (0 when playing alone)."),
        ("static int prg32_kit_peers(void) { return prg32_multiplayer_get_peer_count(); }", ""),
    ],
    "peer_x": [("static int prg32_kit_peer_x(int index) { return prg32_kit_peer_value(index, 0); }", "x of player 'index'")],
    "peer_y": [("static int prg32_kit_peer_y(int index) { return prg32_kit_peer_value(index, 1); }", "y of player 'index'")],
    "peer_look": [("static int prg32_kit_peer_look(int index) { return prg32_kit_peer_value(index, 2); }", "look of player 'index'")],
    "once": [
        *_doc("The buttons held during the previous frame, for the 'was just pressed' blocks.",
              "A button was just pressed when its bit is 1 now and was 0 one frame ago."),
        ("static uint32_t prg32_kit_previous_input;", "one bit per button, like the input value"),
    ],
    "beep": [
        *_doc("prg32_kit_note_from_hz(hz): the MIDI note nearest to a frequency, from C3 (48) to C6 (84).",
              "Each musical semitone multiplies the frequency by about 1.0595. The table lists the",
              "frequency of notes 48..84; the search stops at the first note whose midpoint with",
              "the next note is above hz, which is the nearest note."),
        ("static uint8_t prg32_kit_note_from_hz(int hz) {", ""),
        ("    static const uint16_t frequencies[] = {", "Hz of MIDI notes 48 (C3) to 84 (C6)"),
        "        131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247,",
        "        262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,",
        "        523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988, 1047",
        "    };",
        ("    unsigned int index;", "position in the table"),
        ("    if (hz <= frequencies[0]) return 48;", "at or below C3: the lowest note"),
        ("    for (index = 0; index < 36; ++index) {", "try each pair of neighbouring notes"),
        ("        if (hz < (frequencies[index] + frequencies[index + 1]) / 2) return (uint8_t)(48 + index);",
         "below the midpoint: this note is the nearest"),
        ("    }", ""),
        ("    return 84;", "above every midpoint: the highest note"),
        ("}", ""),
    ],
    "number": [
        *_doc("prg32_kit_format_number(out, value): write a whole number as decimal text.",
              "A cartridge has no printf, so the digits are made by hand: value % 10 is the last",
              "digit and value / 10 removes it. The digits come out last-first, so they are",
              "collected in digits[] and then copied in reverse order.",
              "'0' + d turns a digit 0..9 into its character code ('0' is 48 in ASCII).",
              "Result: the number of characters written (the text ends with a 0 byte).",
              "",
              "prg32_kit_draw_number(x, y, value, fg, bg): format the number, then draw the text."),
        ("static int prg32_kit_format_number(char *out, int value) {", ""),
        ("    char digits[11];", "room for the 10 digits of the largest int"),
        ("    unsigned int magnitude = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;",
         "the size without the sign; unsigned also works for -2147483648"),
        ("    int count = 0;", "digits collected so far"),
        ("    int length = 0;", "characters written to out"),
        ("    do {", "do ... while runs at least once, so 0 prints as \"0\""),
        ("        digits[count++] = (char)('0' + magnitude % 10u);", "keep the last digit"),
        ("        magnitude /= 10u;", "and remove it from the number"),
        ("    } while (magnitude != 0u && count < 10);", "until no digits are left"),
        ("    if (value < 0) out[length++] = '-';", "a minus sign first"),
        ("    while (count > 0) out[length++] = digits[--count];", "copy the digits, last one first"),
        ("    out[length] = '\\0';", "C text always ends with a 0 byte"),
        ("    return length;", ""),
        ("}", ""),
        ("static void prg32_kit_draw_number(int x, int y, int value, uint16_t fg, uint16_t bg) {", ""),
        ("    char text[12];", "sign + 10 digits + the final 0 byte"),
        ("    prg32_kit_format_number(text, value);", "number -> text"),
        ("    prg32_gfx_text8(x, y, text, fg, bg);", "text -> screen"),
        ("}", ""),
    ],
    "band": [
        *_doc("prg32_kit_band(text, has_value, value): show text (and a number) in the firmware status band.",
              "Redrawing the band is slow, so the new text is built in band_now and compared",
              "with band_shown (what is on screen): the firmware is called only when they differ."),
        ("static char prg32_kit_band_now[48];", "the text wanted this frame"),
        ("static char prg32_kit_band_shown[48];", "the text currently shown"),
        ("static void prg32_kit_band(const char *text, int has_value, int value) {", ""),
        ("    int length = 0;", "characters in band_now"),
        ("    int index;", "position used by the comparison loops"),
        ("    while (text[length] != '\\0' && length < 32) {", "copy at most 32 characters of text"),
        ("        prg32_kit_band_now[length] = text[length];", ""),
        ("        ++length;", ""),
        ("    }", ""),
        ("    if (has_value) {", "a number follows the text"),
        ("        prg32_kit_band_now[length++] = ' ';", "a space between text and number"),
        ("        length += prg32_kit_format_number(prg32_kit_band_now + length, value);", "append the digits"),
        ("    }", ""),
        ("    prg32_kit_band_now[length] = '\\0';", "end of text"),
        ("    for (index = 0; index <= length; ++index) {", "compare character by character"),
        ("        if (prg32_kit_band_now[index] != prg32_kit_band_shown[index]) break;", "first difference: stop"),
        ("    }", ""),
        ("    if (index > length) return;", "no difference: the band already shows this text"),
        ("    for (index = 0; index <= length; ++index) prg32_kit_band_shown[index] = prg32_kit_band_now[index];",
         "remember what will be shown"),
        ("    prg32_band_set_game_info(prg32_kit_band_now);", "ask the firmware to redraw the band"),
        ("}", ""),
    ],
    "frame_rect": [
        *_doc("prg32_kit_draw_frame(x, y, w, h, color): a rectangle outline made of four 1-pixel-thick bars."),
        ("static void prg32_kit_draw_frame(int x, int y, int w, int h, uint16_t color) {", ""),
        ("    if (w <= 0 || h <= 0) return;", "nothing to draw"),
        ("    prg32_gfx_rect(x, y, w, 1, color);", "top edge"),
        ("    prg32_gfx_rect(x, y + h - 1, w, 1, color);", "bottom edge"),
        ("    prg32_gfx_rect(x, y, 1, h, color);", "left edge"),
        ("    prg32_gfx_rect(x + w - 1, y, 1, h, color);", "right edge"),
        ("}", ""),
    ],
    "line": [
        *_doc("prg32_kit_draw_line(x1, y1, x2, y2, color): Bresenham's line algorithm (1965).",
              "The line moves one pixel at a time. 'error' measures how far the drawn pixels are",
              "from the true line; when twice the error passes dy (or dx) the line steps in x",
              "(or y). Only additions and comparisons are needed: no division, no fractions.",
              "dx is the horizontal distance; dy is the vertical distance made negative, which",
              "lets one error value handle both directions. At most 2048 pixels are drawn."),
        ("static void prg32_kit_draw_line(int x1, int y1, int x2, int y2, uint16_t color) {", ""),
        ("    int dx = x2 > x1 ? x2 - x1 : x1 - x2;", "dx = |x2 - x1|"),
        ("    int dy = y2 > y1 ? y1 - y2 : y2 - y1;", "dy = -|y2 - y1| (negative on purpose)"),
        ("    int sx = x1 < x2 ? 1 : -1;", "step direction in x"),
        ("    int sy = y1 < y2 ? 1 : -1;", "step direction in y"),
        ("    int error = dx + dy;", "starting error"),
        ("    int steps = 0;", "safety counter"),
        ("    while (steps++ < 2048) {", "at most 2048 pixels"),
        ("        int twice;", ""),
        ("        prg32_gfx_pixel(x1, y1, color);", "draw the current pixel"),
        ("        if (x1 == x2 && y1 == y2) break;", "reached the end point: stop"),
        ("        twice = 2 * error;", ""),
        ("        if (twice >= dy) { error += dy; x1 += sx; }", "time to step in x"),
        ("        if (twice <= dx) { error += dx; y1 += sy; }", "time to step in y"),
        ("    }", ""),
        ("}", ""),
    ],
    "circle": [
        *_doc("prg32_kit_draw_circle(cx, cy, r, color, filled): the midpoint circle algorithm.",
              "A circle is symmetric, so the algorithm walks only one eighth of it (from the",
              "top going right, while x >= y) and mirrors every point eight times. 'error' tells",
              "whether the midpoint between two candidate pixels is inside or outside the circle.",
              "A filled circle draws horizontal bars between mirrored points instead of pixels.",
              "The radius is limited to 200 pixels."),
        ("static void prg32_kit_draw_circle(int cx, int cy, int r, uint16_t color, int filled) {", ""),
        ("    int x;", "horizontal distance from the centre"),
        ("    int y = 0;", "vertical distance from the centre"),
        ("    int error;", "inside (< 0) or outside (>= 0) the circle"),
        ("    if (r < 0) return;", "no circle with a negative radius"),
        ("    if (r > 200) r = 200;", "keep the work per frame small"),
        ("    x = r;", "start at the rightmost point"),
        ("    error = 1 - r;", ""),
        ("    while (x >= y) {", "one eighth of the circle"),
        ("        if (filled) {", ""),
        ("            prg32_gfx_rect(cx - x, cy + y, 2 * x + 1, 1, color);", "bar below the centre"),
        ("            prg32_gfx_rect(cx - x, cy - y, 2 * x + 1, 1, color);", "bar above the centre"),
        ("            prg32_gfx_rect(cx - y, cy + x, 2 * y + 1, 1, color);", "bar near the bottom"),
        ("            prg32_gfx_rect(cx - y, cy - x, 2 * y + 1, 1, color);", "bar near the top"),
        ("        } else {", ""),
        ("            prg32_gfx_pixel(cx + x, cy + y, color);", "the eight mirror images of (x, y)"),
        ("            prg32_gfx_pixel(cx - x, cy + y, color);", ""),
        ("            prg32_gfx_pixel(cx + x, cy - y, color);", ""),
        ("            prg32_gfx_pixel(cx - x, cy - y, color);", ""),
        ("            prg32_gfx_pixel(cx + y, cy + x, color);", ""),
        ("            prg32_gfx_pixel(cx - y, cy + x, color);", ""),
        ("            prg32_gfx_pixel(cx + y, cy - x, color);", ""),
        ("            prg32_gfx_pixel(cx - y, cy - x, color);", ""),
        ("        }", ""),
        ("        ++y;", "always one step down"),
        ("        if (error < 0) {", "midpoint inside: keep x"),
        ("            error += 2 * y + 1;", ""),
        ("        } else {", "midpoint outside: also step left"),
        ("            --x;", ""),
        ("            error += 2 * (y - x) + 1;", ""),
        ("        }", ""),
        ("    }", ""),
        ("}", ""),
    ],
    "bitmap": [
        *_doc("prg32_kit_draw_bitmap(x, y, rows, color, scale): draw an 8x8 picture.",
              "Each row is one byte. Bit 7 (value 0x80) is the left pixel and bit 0 the right one,",
              "so (0x80 >> col) selects column col and & tests whether that pixel is lit.",
              "Every lit pixel becomes a scale x scale square; scale is kept inside 1..8."),
        ("static void prg32_kit_draw_bitmap(int x, int y, const uint8_t *rows, uint16_t color, int scale) {", ""),
        ("    int row;", ""),
        ("    int col;", ""),
        ("    if (scale < 1) scale = 1;", ""),
        ("    if (scale > 8) scale = 8;", ""),
        ("    for (row = 0; row < 8; ++row) {", "rows from top to bottom"),
        ("        for (col = 0; col < 8; ++col) {", "columns from left to right"),
        ("            if (rows[row] & (0x80u >> col)) prg32_gfx_rect(x + col * scale, y + row * scale, scale, scale, color);",
         "lit pixel: draw its square"),
        ("        }", ""),
        ("    }", ""),
        ("}", ""),
    ],
}
HELPERS: dict[str, list[str]] = {name: annotate_c(rows) for name, rows in _HELPER_SOURCE.items()}
# Helpers that call other helpers.
_HELPER_NEEDS = {"cos": {"sin"}, "band": {"number"}, "peer_x": {"peer_value"}, "peer_y": {"peer_value"},
                 "peer_look": {"peer_value"}}
_HELPER_ORDER = ["list_get", "list_set", "list_fill", "div", "mod", "abs", "sign", "min", "max", "clamp", "sin", "cos", "rgb", "random", "frame", "tile",
                 "peer_value", "peers", "peer_x", "peer_y", "peer_look", "once", "beep", "number", "band", "frame_rect", "line", "circle", "bitmap"]


def needed_helpers(needs: set[str]) -> list[str]:
    """The helpers a game needs, with the helpers they call, in file order."""
    wanted = set(needs)
    for name in list(wanted):
        wanted |= _HELPER_NEEDS.get(name, set())
    return [name for name in _HELPER_ORDER if name in wanted]


def rgb565_words(value: int) -> str:
    """Explain an RGB565 colour: its 5-bit red, 6-bit green and 5-bit blue parts."""
    return f"red {value >> 11}/31, green {(value >> 5) & 0x3F}/63, blue {value & 0x1F}/31"


def bitmap_picture(rows: tuple[int, ...]) -> list[str]:
    """Draw an 8x8 picture as text, one row per line: '#' is a lit pixel."""
    return ["".join("#" if row & (0x80 >> col) else "." for col in range(8)) + f"   0x{row:02X}" for row in rows]


def _statements_read_input(statements: Any) -> bool:
    """True when a statement list (or a nested one) has a button block."""
    for stmt in statements if isinstance(statements, list) else []:
        if not isinstance(stmt, dict):
            continue
        if stmt.get("op") in {"if_button", "if_button_once"}:
            return True
        if any(_statements_read_input(stmt.get(key)) for key in ("then", "else", "body")):
            return True
    return False


class _Emitter:
    def __init__(self, game_ir: dict[str, Any], explain: bool = True):
        self.lists = {state_name(item.get("name")): max(1, min(MAX_LIST_SIZE, c_int(item.get("size"), 1)))
                      for item in game_ir.get("lists", []) if isinstance(item, dict)}
        self.needs: set[str] = set()
        self.bitmaps: dict[tuple[int, ...], str] = {}
        self.title = str(game_ir.get("title") or "PRG32 Game")
        self.python = is_python(game_ir)
        self.explain = explain
        self.explained: set[str] = set()
        # id(statement) -> block id: wrap each statement's lines in source-map markers.
        self.trace: dict[int, str] | None = None

    def x(self, text: Any) -> str:
        """Translate one IR expression to C."""
        checked = check_expr(text, self.lists) if text not in (None, "") else None
        return expr_to_c(checked, self.lists, self.needs) if checked else "0"

    def show(self, text: Any) -> str:
        """The checked student form of one IR expression, for explanations."""
        return (check_expr(text, self.lists) if text not in (None, "") else None) or "0"

    def bitmap(self, rows: Any) -> str:
        values = tuple((c_int(v) & 0xFF) for v in (rows if isinstance(rows, list) else [])[:8])
        values = values + (0,) * (8 - len(values))
        if values not in self.bitmaps:
            self.bitmaps[values] = f"prg32_kit_bitmap_{len(self.bitmaps)}"
        return self.bitmaps[values]

    def block(self, statements: Any, indent: str, depth: int) -> list[str]:
        lines: list[str] = []
        for stmt in statements if isinstance(statements, list) else []:
            if not isinstance(stmt, dict):
                continue
            mark = self.trace.get(id(stmt)) if self.trace else None
            if mark:
                lines.append(MAP_BEGIN + mark)
            if self.explain:
                sentence = narrate(describe_statement(stmt, self.show), self.python)
                if sentence:
                    lines.extend(c_comment_block(sentence, indent))
            code = self.statement(stmt, indent, depth)
            op = stmt.get("op")
            if self.explain and code and op in C_SYNTAX_NOTES and op not in self.explained:
                # The first time a C construct appears, a note beside it explains the syntax.
                self.explained.add(op)
                code = annotate_c([(code[0], C_SYNTAX_NOTES[op])]) + code[1:]
            lines.extend(code)
            if mark:
                lines.append(MAP_END + mark)
        return lines

    def note(self, code: str, text: str) -> list[str]:
        """One code line with its explanation (when explanations are on)."""
        return annotate_c([(code, text if self.explain else "")])

    def statement(self, stmt: dict[str, Any], indent: str, depth: int) -> list[str]:
        op = stmt.get("op")
        x = self.x
        var = state_name(stmt.get("var")) if "var" in stmt else ""
        lst = state_name(stmt.get("list")) if "list" in stmt else ""
        inner = indent + "    "
        if op == "set":
            return [f"{indent}{var} = {x(stmt.get('value'))};"]
        if op == "change":
            return [f"{indent}{var} += {x(stmt.get('delta'))};"]
        if op == "clamp":
            low, high = x(stmt.get("low")), x(stmt.get("high"))
            return [f"{indent}if ({var} < {low}) {{ {var} = {low}; }}",
                    f"{indent}if ({var} > {high}) {{ {var} = {high}; }}"]
        if op == "wrap":
            low, high = x(stmt.get("low")), x(stmt.get("high"))
            return [f"{indent}if ({var} < {low}) {{ {var} += ({high}) - ({low}) + 1; }}",
                    f"{indent}else if ({var} > {high}) {{ {var} -= ({high}) - ({low}) + 1; }}"]
        if op == "random":
            self.needs.add("random")
            return [f"{indent}{var} = prg32_kit_random({x(stmt.get('low'))}, {x(stmt.get('high'))});"]
        if op == "if":
            cond = x(stmt.get("cond"))
            lines = [f"{indent}if ({cond}) {{", *self.block(stmt.get("then"), inner, depth)]
            if "else" in stmt:
                lines += [*self.note(f"{indent}}} else {{", "otherwise (the condition was 0)"),
                          *self.block(stmt.get("else"), inner, depth)]
            return lines + [*self.note(f"{indent}}}", f"end of if ({self.show(stmt.get('cond'))[:40]})")]
        if op == "repeat":
            k, end, n = f"prg32_kit_step{depth}", f"prg32_kit_last{depth}", f"prg32_kit_count{depth}"
            return [
                *self.note(f"{indent}{{", "a block: the loop's own variables exist only inside it"),
                f"{inner}/* repeat {var} from {c_comment(stmt.get('from'))} to {c_comment(stmt.get('to'))} (at most {MAX_REPEAT} times) */",
                *self.note(f"{inner}int {k};", f"the value {var} takes in this round"),
                *self.note(f"{inner}int {end} = {x(stmt.get('to'))};", "the last value, computed once before the loop"),
                *self.note(f"{inner}int {n} = 0;", "rounds done so far (the safety limit)"),
                f"{inner}for ({k} = {x(stmt.get('from'))}; {k} <= {end} && {n} < {MAX_REPEAT}; ++{k}, ++{n}) {{",
                *self.note(f"{inner}    {var} = {k};", "the block's variable takes this round's value"),
                *self.block(stmt.get("body"), inner + "    ", depth + 1),
                *self.note(f"{inner}}}", "end of round: count up, test again"),
                *self.note(f"{indent}}}", "end of repeat"),
            ]
        if op in {"list_fill", "list_set", "list_change"}:
            if lst not in self.lists:
                return [f"{indent}/* unknown list {c_comment(lst)} */"]
            size = self.lists[lst]
            if op == "list_fill":
                self.needs.add("list_fill")
                return [f"{indent}prg32_kit_fill({lst}, {size}, {x(stmt.get('value'))});"]
            index = x(stmt.get("index"))
            self.needs.add("list_set")
            if op == "list_set":
                return [f"{indent}prg32_kit_set({lst}, {size}, {index}, {x(stmt.get('value'))});"]
            self.needs.add("list_get")
            return [f"{indent}prg32_kit_set({lst}, {size}, {index}, prg32_kit_get({lst}, {size}, {index}) + {x(stmt.get('delta'))});"]
        if op == "if_button":
            return [f"{indent}if (input & {button_macro(stmt.get('button'))}) {{",
                    *self.block(stmt.get("then"), inner, depth), *self.note(f"{indent}}}", "end of if button")]
        if op == "if_button_once":
            self.needs.add("once")
            button = button_macro(stmt.get("button"))
            return [f"{indent}if ((input & {button}) && !(prg32_kit_previous_input & {button})) {{",
                    *self.block(stmt.get("then"), inner, depth), *self.note(f"{indent}}}", "end of if just pressed")]
        if op == "if_touching":
            args = ", ".join(x(stmt.get(k)) for k in ("ax", "ay", "aw", "ah", "bx", "by", "bw", "bh"))
            return [f"{indent}if (prg32_sprite_hitbox({args})) {{", *self.block(stmt.get("then"), inner, depth),
                    *self.note(f"{indent}}}", "end of if touching")]
        if op == "clear":
            return [f"{indent}prg32_gfx_clear({color_macro(stmt.get('color'))});"]
        if op == "rect":
            return [f"{indent}prg32_gfx_rect({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('w'))}, {x(stmt.get('h'))}, {color_macro(stmt.get('color'))});"]
        if op == "rect_rgb":
            return [f"{indent}prg32_gfx_rect({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('w'))}, {x(stmt.get('h'))}, {x(stmt.get('rgb'))});"]
        if op == "frame":
            self.needs.add("frame_rect")
            return [f"{indent}prg32_kit_draw_frame({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('w'))}, {x(stmt.get('h'))}, {color_macro(stmt.get('color'))});"]
        if op == "pixel":
            return [f"{indent}prg32_gfx_pixel({x(stmt.get('x'))}, {x(stmt.get('y'))}, {color_macro(stmt.get('color'))});"]
        if op == "line":
            self.needs.add("line")
            return [f"{indent}prg32_kit_draw_line({x(stmt.get('x1'))}, {x(stmt.get('y1'))}, {x(stmt.get('x2'))}, {x(stmt.get('y2'))}, {color_macro(stmt.get('color'))});"]
        if op == "circle":
            self.needs.add("circle")
            return [f"{indent}prg32_kit_draw_circle({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('r'))}, {color_macro(stmt.get('color'))}, {1 if stmt.get('fill') else 0});"]
        if op == "bitmap":
            self.needs.add("bitmap")
            return [f"{indent}prg32_kit_draw_bitmap({x(stmt.get('x'))}, {x(stmt.get('y'))}, {self.bitmap(stmt.get('rows'))}, {color_macro(stmt.get('color'))}, {x(stmt.get('scale'))});"]
        if op == "sprite8":
            return [f"{indent}prg32_sprite_draw_8x8({x(stmt.get('x'))}, {x(stmt.get('y'))}, {self.bitmap(stmt.get('rows'))}, {color_macro(stmt.get('fg'))}, {color_macro(stmt.get('bg'))});"]
        if op == "palette_set":
            return [f"{indent}prg32_palette_set({x(stmt.get('index'))}, {color_macro(stmt.get('color'))});"]
        if op == "clear_indexed":
            return [f"{indent}prg32_gfx_clear_indexed({x(stmt.get('index'))});"]
        if op == "rect_indexed":
            return [f"{indent}prg32_gfx_rect_indexed({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('w'))}, {x(stmt.get('h'))}, {x(stmt.get('index'))});"]
        if op == "pixel_indexed":
            return [f"{indent}prg32_gfx_pixel_indexed({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('index'))});"]
        if op == "text":
            return [f"{indent}prg32_gfx_text8({x(stmt.get('x'))}, {x(stmt.get('y'))}, \"{c_string(stmt.get('text'))}\", "
                    f"{color_macro(stmt.get('fg'))}, {color_macro(stmt.get('bg', 'BLACK'))});"]
        if op == "number":
            self.needs.add("number")
            return [f"{indent}prg32_kit_draw_number({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('value'))}, "
                    f"{color_macro(stmt.get('fg'))}, {color_macro(stmt.get('bg', 'BLACK'))});"]
        if op == "tile_define":
            return [f"{indent}prg32_tile_define({x(stmt.get('id'))}, {self.bitmap(stmt.get('rows'))}, "
                    f"{color_macro(stmt.get('fg'))}, {color_macro(stmt.get('bg', 'BLACK'))});"]
        if op == "playfield_clear":
            return [f"{indent}prg32_playfield_clear({x(stmt.get('layer'))}, {x(stmt.get('id'))});"]
        if op == "playfield_put":
            return [f"{indent}prg32_playfield_put({x(stmt.get('layer'))}, {x(stmt.get('tx'))}, "
                    f"{x(stmt.get('ty'))}, {x(stmt.get('id'))});"]
        if op == "playfield_scroll":
            return [f"{indent}prg32_playfield_scroll({x(stmt.get('layer'))}, {x(stmt.get('x'))}, {x(stmt.get('y'))});"]
        if op == "playfield_camera":
            return [f"{indent}prg32_playfield_camera({x(stmt.get('x'))}, {x(stmt.get('y'))});"]
        if op == "playfield_draw":
            return [f"{indent}prg32_playfield_draw({x(stmt.get('layer'))}, {1 if stmt.get('transparent') else 0});"]
        if op == "beep":
            self.needs.add("beep")
            return [f"{indent}prg32_audio_note(0, 0, prg32_kit_note_from_hz({x(stmt.get('freq'))}), 255, {x(stmt.get('ms'))});"]
        if op == "note":
            return [f"{indent}prg32_audio_note({x(stmt.get('channel'))}, 0, {x(stmt.get('note'))}, 255, {x(stmt.get('ms'))});"]
        if op == "note_off":
            return [f"{indent}prg32_audio_note_off({x(stmt.get('channel'))});"]
        if op == "volume":
            return [f"{indent}prg32_audio_set_master_volume({x(stmt.get('volume'))});"]
        if op == "sfx":
            return [f"{indent}prg32_audio_note({int(stmt['voice'])}, {int(stmt['instrument'])}, {int(stmt['note'])}, "
                    f"{int(stmt['volume'])}, {int(stmt['ms'])});"]
        if op == "track_play":
            return [f"{indent}prg32_audio_play_track({int(stmt['track'])});"]
        if op == "track_stop":
            return [f"{indent}prg32_audio_stop_track();"]
        if op == "sound_stop_all":
            return [f"{indent}prg32_audio_stop_all();"]
        if op == "band":
            self.needs.add("band")
            has_value = "value" in stmt
            value = x(stmt.get("value")) if has_value else "0"
            return [f"{indent}prg32_kit_band(\"{c_string(stmt.get('text'))}\", {1 if has_value else 0}, {value});"]
        if op == "score_submit":
            return [f"{indent}prg32_score_submit_current_player(\"{c_string(self.title[:23])}\", {x(stmt.get('value'))});"]
        if op == "scoreboard":
            return [f"{indent}prg32_scoreboard_show(\"{c_string(self.title[:23])}\", \"{c_string(stmt.get('title'))}\");"]
        if op == "splash":
            return [f"{indent}prg32_splash_show_game(\"{c_string(stmt.get('title'))}\", \"{c_string(stmt.get('subtitle'))}\", "
                    f"{x(stmt.get('ms'))}, {color_macro(stmt.get('bg'))}, {color_macro(stmt.get('fg'))}, "
                    f"{color_macro(stmt.get('accent'))});"]
        if op == "player_name":
            return [f"{indent}prg32_score_player_prompt();"]
        if op == "mp_join":
            return [f"{indent}prg32_multiplayer_join(\"{c_string(self.title[:31])}\", PRG32_MP_FLAG_ENABLE);"]
        if op == "mp_share":
            return [f"{indent}prg32_multiplayer_set_local_state({x(stmt.get('x'))}, {x(stmt.get('y'))}, {x(stmt.get('look'))}, 0);",
                    f"{indent}prg32_multiplayer_tick();"]
        if op == "comment":
            return [f"{indent}/* {c_comment(stmt.get('text'))} */"]
        return [f"{indent}/* Unsupported op: {c_comment(op)} */"]


def _helper_lines(needs: set[str]) -> list[str]:
    lines: list[str] = []
    for name in needed_helpers(needs):
        lines.extend(HELPERS[name])
        lines.append("")
    return lines


def _section(title: str, *text: str) -> list[str]:
    """A numbered section banner in the generated C."""
    rule = "=" * 74
    return ["/* " + rule, f" * {title}", *(f" * {line}".rstrip() for line in text), " * " + rule + " */"]


def ir_to_c(game_ir: dict[str, Any], explain: bool = True, trace: dict[int, str] | None = None) -> str:
    """Write the game IR as freestanding C, explained line by line for students.

    ``explain=False`` leaves out the explanations (the slide decks show short excerpts).
    ``trace`` (from blocks_to_ir_traced) adds source-map marker lines; source_maps.py
    removes them again, leaving exactly the normal output.
    """
    prefix = c_identifier(game_ir.get("entry_prefix"), "prg32_game")
    emitter = _Emitter(game_ir, explain)
    emitter.trace = trace

    def annotate(rows: list[Any]) -> list[str]:
        if explain:
            return annotate_c(rows)
        return [row if isinstance(row, str) else row[0] for row in rows]

    def section(*text: str) -> list[str]:
        return _section(*text) if explain else []

    def doc(*text: str) -> list[str]:
        return _doc(*text) if explain else []
    state = [(state_name(item.get("name")), int32(c_int(item.get("initial"), 0)))
             for item in game_ir.get("state", []) if isinstance(item, dict)]
    phases = {phase: game_ir.get(phase, []) for phase in ("init", "update", "draw")}
    bodies = {phase: emitter.block(statements, "    ", 0) for phase, statements in phases.items()}
    reads_input = {phase: _statements_read_input(statements) for phase, statements in phases.items()}
    needs = emitter.needs
    if emitter.lists:
        needs.add("list_fill")  # init clears every list with prg32_kit_fill
    title = c_comment(emitter.title)

    source = ("the saved PRG32 Python (game.py)", "Edit game.py") if emitter.python \
        else ("the saved Blocks JSON", "Edit the blocks")
    phase_names = ("def init()", "def update()", "def draw()") if emitter.python \
        else ("'when game starts' blocks", "'every frame' blocks", "'draw' blocks")
    lines: list[str] = [
        "/*",
        f" * {title}",
        f" * Generated by {GENERATED_BY} from {source[0]}.",
        f" * {source[1]}, then regenerate this file.",
    ]
    guide = [
        " *",
        " * HOW TO READ THIS FILE",
        " * A PRG32 cartridge is a small program that the PRG32 firmware loads into memory",
        " * and calls through three functions (the 'cartridge lifecycle'):",
        f" *   {prefix}_init()    once, when the game starts ({phase_names[0]});",
        f" *   {prefix}_update()  once per frame, to move things ({phase_names[1]});",
        f" *   {prefix}_draw()    once per frame, after update, to paint the screen ({phase_names[2]}).",
        " * The file has five parts: 1 constants, 2 kit helper functions, 3 pictures,",
        " * 4 game state (the variables), 5 the three lifecycle functions.",
        (" * Above every step's code a comment repeats the Python statement in words; beside many"
         if emitter.python else " * Above every block's code a comment repeats the block in words; beside many lines"),
        (" * lines a second comment explains that line."
         if emitter.python else " * a second comment explains that line."),
        " *",
        " * RULES THIS C FOLLOWS",
        " * - Freestanding C99: no standard library (no printf, no malloc), because a",
        " *   cartridge runs without an operating system. Everything it needs is either in",
        " *   the firmware (functions named prg32_...) or written below in part 2.",
        " * - Every number is a 32-bit whole number (int). There is no floating point:",
        " *   7 / 2 is 3, and fractions are made with scaled numbers (256 means 1.0).",
        (" * - The same game.py always produces exactly the same file." if emitter.python
         else " * - The same blocks always produce exactly the same file."),
    ]
    if explain:
        lines.extend(guide)
    lines += [
        " */",
        ("#include <stdint.h>", "exact-size types: uint8_t, int16_t, uint32_t, ..."),
        ("#include \"prg32.h\"", "the PRG32 firmware functions and constants"),
        "",
        *section("1. CONSTANTS",
                  "#define gives a name to a number. #ifndef ... #endif defines the name only when",
                  "prg32.h has not already done so. Colours are RGB565 numbers: 16 bits holding",
                  "5 bits of red, 6 of green and 5 of blue."),
    ]
    lines = annotate(lines)
    for macro, value in COLOR_DEFINES.items():
        lines.extend(annotate([f"#ifndef {macro}", (f"#define {macro} {value}", rgb565_words(CONSTANTS[macro])), "#endif"]))
    lines.extend(annotate([
        "#ifndef PRG32_SCREEN_W",
        ("#define PRG32_SCREEN_W PRG32_GAME_W", "the game area is 320 pixels wide"),
        "#endif",
        "#ifndef PRG32_SCREEN_H",
        ("#define PRG32_SCREEN_H PRG32_GAME_H", "and 200 pixels high; (0, 0) is the top-left"),
        "#endif",
        "",
    ]))
    helpers = _helper_lines(needs)
    if helpers:
        lines.extend(section("2. KIT HELPER FUNCTIONS",
                              "Small functions the blocks need, written here so nothing is hidden.",
                              "Only the helpers this game uses are included. 'static' keeps a name",
                              "private to this file."))
        lines.extend(helpers)
    if emitter.bitmaps:
        lines.extend(section("3. PICTURES",
                              "Each 8x8 picture is 8 bytes, one per row; a 1 bit is a lit pixel.",
                              "'const' data never changes, so it can stay in read-only memory."))
        for rows, name in emitter.bitmaps.items():
            if explain:
                lines.append(f"/* {name}:")
                lines.extend(f" *   {row}" for row in bitmap_picture(rows))
                lines.append(" */")
            else:
                lines.append("/* " + "  ".join(row[:8] for row in bitmap_picture(rows)) + " */")
            lines.append(f"static const uint8_t {name}[8] = {{{', '.join(f'0x{row:02X}' for row in rows)}}};")
        lines.append("")
    lines.extend(section("4. GAME STATE",
                          "Variables declared outside any function keep their values from one frame",
                          "to the next. Every variable is a whole number (int); a list is an array."))
    for name, initial in state:
        lines.extend(annotate([(f"static int {name} = {initial};", f"variable {name}, starts at {initial}")]))
    for name, size in emitter.lists.items():
        lines.extend(annotate([(f"static int {name}[{size}];", f"list {name}: items {name}[0] .. {name}[{size - 1}]")]))
    lines.append("")
    lines.extend(section("5. LIFECYCLE FUNCTIONS",
                          "The firmware calls these three functions; their names must start with",
                          f"the entry prefix '{prefix}'. 'void' means: no inputs, no result."))

    read_input = ("    uint32_t input = prg32_input_read();",
                  "every button at once: one bit per button, 1 = held")
    lines.extend(doc(f"{prefix}_init(): runs once when the cartridge starts (and again on restart).",
                      "First every variable and list gets its starting value, then the",
                      "'when game starts' blocks run."))
    lines.append(f"void {prefix}_init(void) {{")
    body: list[Any] = []
    if reads_input["init"]:
        body.append(read_input)
    for name, initial in state:
        body.append((f"    {name} = {initial};", f"{name} starts again from {initial}"))
    for name, size in emitter.lists.items():
        body.append((f"    prg32_kit_fill({name}, {size}, 0);", f"every item of {name} starts at 0"))
    if "frame" in needs:
        body.append(("    prg32_kit_frame_count = 0;", "no frames have finished yet"))
    if "once" in needs:
        body.append(("    prg32_kit_previous_input = prg32_input_read();", "buttons already held do not count as a press"))
    lines.extend(annotate(body))
    lines.extend(bodies["init"])
    lines.append("}")
    lines.append("")

    lines.extend(doc(f"{prefix}_update(): runs once per frame, before drawing.",
                      "Game rules go here: read the buttons, move things, test collisions, keep score."))
    lines.append(f"void {prefix}_update(void) {{")
    if reads_input["update"] or "once" in needs:
        lines.extend(annotate([read_input]))
    lines.extend(bodies["update"])
    tail: list[Any] = []
    if "once" in needs:
        tail.append(("    prg32_kit_previous_input = input;", "remember this frame's buttons for the next frame"))
    if "frame" in needs:
        tail.append(("    prg32_kit_frame_count++;", "one more frame has finished"))
    lines.extend(annotate(tail))
    lines.append("}")
    lines.append("")

    lines.extend(doc(f"{prefix}_draw(): runs once per frame, after update.",
                      "Only painting goes here: the screen shows the state that update computed."))
    lines.append(f"void {prefix}_draw(void) {{")
    if reads_input["draw"]:
        lines.extend(annotate([read_input]))
    lines.extend(bodies["draw"])
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def blocks_to_c(blocks_json: Any, project: dict[str, Any] | None = None, explain: bool = True) -> tuple[dict[str, Any], str]:
    game_ir = blocks_to_ir(blocks_json, project)
    return game_ir, ir_to_c(game_ir, explain)


# Source-map marker lines (ADR-0015): only ever present while source_maps.py builds a map.
MAP_BEGIN = "\x00prg32-map-begin "
MAP_END = "\x00prg32-map-end "

_HEX_COLOR_RE = re.compile(r"^#?([0-9A-Fa-f]{3}|[0-9A-Fa-f]{6})$")
MAX_SPRITE_SIDE = 128


def _rgb565(hex_color: Any) -> int:
    if not isinstance(hex_color, str) or hex_color == "transparent":
        return 0
    match = _HEX_COLOR_RE.match(hex_color.strip())
    if not match:
        return 0
    text = match.group(1)
    if len(text) == 3:
        text = "".join(ch * 2 for ch in text)
    r = int(text[0:2], 16)
    g = int(text[2:4], 16)
    b = int(text[4:6], 16)
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def sprite_to_c(sprite: dict[str, Any]) -> str:
    name = c_identifier(sprite.get("name"), "sprite")
    width = max(1, min(MAX_SPRITE_SIDE, c_int(sprite.get("width"), 16) or 16))
    height = max(1, min(MAX_SPRITE_SIDE, c_int(sprite.get("height"), 16) or 16))
    if sprite.get("palette"):
        return indexed_sprite_to_c(name, width, height, sprite["palette"])
    pixels = sprite.get("pixels") if isinstance(sprite.get("pixels"), list) else []
    flat: list[int] = []
    for y in range(height):
        row = pixels[y] if y < len(pixels) and isinstance(pixels[y], list) else []
        for x in range(width):
            flat.append(_rgb565(row[x] if x < len(row) else None))
    lines = [
        "/* Sprite generated by PRG32-Construction-Kit. */",
        "#include <stdint.h>",
        f"#define {name.upper()}_WIDTH {width}",
        f"#define {name.upper()}_HEIGHT {height}",
        f"static const uint16_t {name}_pixels[{width * height}] = {{",
    ]
    for y in range(height):
        part = flat[y * width : (y + 1) * width]
        lines.append("    " + ", ".join(f"0x{value:04X}" for value in part) + ("," if y < height - 1 else ""))
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def indexed_sprite_to_c(name: str, width: int, height: int, palette: Any) -> str:
    """An indexed sprite as the PRG32 draws it: its RGB565 palette, its packed indices and the
    prg32_indexed_sprite_t that ties them together (prg32.h; sprite_palette.py)."""
    from .sprite_palette import bits_per_pixel, checked, packed

    palette = checked(palette, width, height)
    colors, bits = palette["colors"], bits_per_pixel(len(palette["colors"]))
    data = packed(palette)
    transparent = palette["transparent_index"]
    lines = [
        f"/* Sprite generated by PRG32-Construction-Kit: {width}x{height}, {len(colors)} colours, {bits} bit(s) per pixel. */",
        "#include <stdint.h>",
        '#include "prg32.h"',
        f"#define {name.upper()}_WIDTH {width}",
        f"#define {name.upper()}_HEIGHT {height}",
        "/* The palette: one RGB565 colour per entry"
        + (f"; entry {transparent} is transparent (not drawn). */" if transparent >= 0 else ". */"),
        f"static const uint16_t {name}_palette[{len(colors)}] = {{",
    ]
    for start in range(0, len(colors), 8):
        part = colors[start:start + 8]
        lines.append("    " + ", ".join(f"0x{_rgb565(color):04X}" for color in part)
                     + ("," if start + 8 < len(colors) else "") + f"  /* {', '.join(part)} */")
    lines += ["};",
              f"/* The pixels: one palette entry each, {bits} bit(s), packed most significant first, row after row"
              f" ({width * height} pixels in {len(data)} bytes instead of {width * height * 2}). */",
              f"static const uint8_t {name}_indices[{len(data)}] = {{"]
    for start in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in data[start:start + 16])
                     + ("," if start + 16 < len(data) else ""))
    lines += ["};",
              f"/* Draw it with prg32_sprite_draw_indexed(x, y, &{name}_sprite, 0); */",
              f"static const prg32_indexed_sprite_t {name}_sprite = {{",
              f"    {name}_indices, {name}_palette, {width}, {height}, 1, {len(colors)}, PRG32_SPRITE_BPP_{bits}, {transparent}",
              "};", ""]
    return "\n".join(lines)
