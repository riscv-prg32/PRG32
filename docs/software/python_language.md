# PRG32 Python

PRG32 Python is a small, typed part of Python for writing PRG32 cartridges. It
comes from [PRG32-Construction-Kit](https://github.com/riscv-prg32/PRG32-Construction-Kit)
(ADR-0026, language version 1) and is now part of the SDK, so a `game.py`
builds from the command line exactly as it does in the kit's IDE.

No Python runs on the console, and the SDK never imports or runs `game.py`.
The SDK reads the file as text, checks it, and translates it into C (or RISC-V
assembly) that the normal [cartridge builder](cartridges.md) compiles:

```text
game.py
  -> ast.parse with size and nesting limits        prg32/python/compiler.py
  -> allow-list and static type checks             prg32/python/compiler.py
  -> game IR (prg32-construction-kit-game-1.1)     the kit's Blocks use the same IR
  -> commented C or commented RISC-V assembly      prg32/python/generator.py, asm_generator.py
  -> portable .prg32 cartridge                     prg32/cartridge/build_cartridge.py
```

A PRG32 Python program is also valid Python 3 syntax, but some operations
behave like the RISC-V processor instead of desktop Python. They are marked
**Cartridge rule** below. For a guided first game, follow the
[Python game tutorial](../learn/tutorial_python_game.md).

## Commands

| Command | What it does |
|---|---|
| `python3 -m prg32 python new game.py` | writes the starter game (`--force` overwrites) |
| `python3 -m prg32 python check game.py` | lists problems as `file:line:column: error [code]: message`; `--json` for tools; exit status 1 when there are problems |
| `python3 -m prg32 python to-c game.py --out game.c` | writes the commented C (prints it without `--out`); `--no-explain` drops the explanations |
| `python3 -m prg32 python to-asm game.py --out game.S` | writes the commented RV32IM assembly |
| `python3 -m prg32 python build game.py --out game.prg32` | writes `game.c` beside the cartridge and builds it; `--asm` builds from `game.S` instead |

`check`, `to-c`, `to-asm` and `build` accept `--name` (the title, by default
the game's folder name), `--entry-prefix` (by default made from the name) and
`--author`. `build` also accepts the cartridge options `--cart-ram-kib`,
`--audio-block`, `--required-feature`, `--tool-prefix`, `--march` and `--mabi`
with the same meaning as in `python3 -m prg32 cartridge build`. Features the
game uses are added by the compiler: `tile(...)` adds `tilemap`, and the
`peer_*`/`peers` values add `multiplayer` (which also sets the multiplayer
header flag). The architecture option is informational, because a portable
cartridge runs on ESP32-C6, QEMU and PRG32-QT.

`build` needs the RISC-V toolchain (`source $HOME/esp-idf/export.sh`). The
other commands need only Python 3.9 or newer.

The cartridge is then uploaded like any other:
[QEMU](../usage/qemu.md), [ESP32-C6](cartridges.md#2-upload-to-board-esp32-c6),
or the [device protocol](usb_protocol.md).

## A Whole Game

```python
"""My PRG32 game."""
import prg32

player_x: int = 150
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
    prg32.rect(player_x, 180, 16, 16, prg32.YELLOW)
    prg32.number(score, 8, 8)
```

Every game has `import prg32` and the three lifecycle functions `init`,
`update` and `draw`, each written `def name() -> None:`. They become the
cartridge entry points `<prefix>_init`, `<prefix>_update` and `<prefix>_draw`
described in the [cartridge guide](cartridges.md#architecture-and-format). A
docstring under a `def` becomes a comment in the generated C.

## Variables and Types

| Type | Meaning | Example |
|---|---|---|
| `int` | a whole number from -2147483648 to 2147483647 | `lives: int = 3` |
| `bool` | `True` or `False` | `playing: bool = True` |
| `list[int]` | a fixed number of `int`s, counted from 0 | `bricks: list[int] = [0] * 10` or `[3, 1, 4]` |

- Every variable has a type and a starting value the first time it is written.
- Starting values outside functions are fixed numbers, `True`/`False`,
  `prg32` constants or earlier variables: `HALF: int = WIDTH // 2`.
- To change a module variable inside a function, write `global name` at the
  start of the function. List items can be changed without `global`.
- A variable declared inside a function (`n: int = 0`) becomes
  `<function>_n` in the C, so its name does not clash with other functions.
- Names use English letters, digits and `_`, and are not C keywords. `WIDTH`
  and `width` are the same name in the C, so only one may exist.
- **Cartridge rule:** numbers wrap around at 32 bits: `2147483647 + 1` is
  `-2147483648`.
- **Cartridge rule:** a list has 1 to 1024 items. Reading outside it gives 0;
  writing outside it does nothing.

## Statements

| PRG32 Python | Notes |
|---|---|
| `x = expression` | `+=`, `-=`, `*=`, `//=`, `%=`, `<<=`, `>>=`, `&=`, `\|=`, `^=` work too |
| `items[i] = expression` | augmented assignments work too |
| `if ...:` / `elif ...:` / `else:` | the test is a `bool`, or an `int` (true when not 0) |
| `for i in range(stop):` / `for i in range(start, stop):` | **Cartridge rule:** at most 1024 rounds; no step |
| `my_helper(a, b)` | calls one of your own functions |
| `prg32.something(...)` | see [the prg32 module](#the-prg32-module) |
| `pass` | does nothing |
| `return` | only as the last line of a function that gives back nothing |

Refused, with the line and a suggestion: `while`, `break`, `continue`, `try`,
`with`, `class`, `lambda`, `del`, `yield`, `async`, `match`, other imports,
`print`, `eval`, `exec`, `open`, slices, conditional expressions, tuples,
dictionaries, sets, f-strings and floating-point numbers.

## Expressions

| Operators | Meaning |
|---|---|
| `+ - *` | add, subtract, multiply |
| `//` `%` | divide and remainder |
| `<< >> & \| ^ ~` | bit operations |
| `== != < <= > >=` | compare (gives a `bool`) |
| `and or not` | combine `bool`s, stopping early as in Python |

- **Cartridge rule:** `//` and `%` round toward zero, like C and the RISC-V
  `div`/`rem` instructions: `-7 // 2` is `-3` (desktop Python says `-4`) and
  `-7 % 2` is `-1`. `x // 0` is `-1` and `x % 0` is `x`.
- **Cartridge rule:** shifts use only the lowest 5 bits of the count.
- `/` and `**` are refused: write `//`, or multiply.
- `and`, `or` and `not` need `bool` values: `if lives > 0 and playing:`.
- Compare two values at a time: `0 < x and x < 10`, not `0 < x < 10`.
- `True` and `False` are not numbers: `True + 1` is refused.
- `abs(x)`, `min(a, b)` and `max(a, b)` are built in.

## Your Own Functions

```python
def move(dx: int, dy: int) -> None:
    global x, y
    x += dx
    y += dy


def double(v: int) -> int:
    return v * 2
```

- Every parameter has a type, and the function declares its result:
  `-> None`, `-> int` or `-> bool`.
- A function that gives back a value has exactly one line: `return expression`.
- The compiler copies a function's statements where it is called (the C has no
  extra functions), so there is no recursion and calls nest at most 8 deep.
- A value that changes every time it is read (`prg32.random(...)`,
  `prg32.frame()`) cannot be passed to a parameter that is used twice: store it
  in a variable first.

## The prg32 Module

Colours: `prg32.BLACK, WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, ORANGE,
PINK, GRAY, DARK_GRAY, BROWN, PURPLE, NAVY, LIME`. Buttons: `prg32.LEFT, RIGHT,
UP, DOWN, A, B, START, SELECT`. The `prg32.P2_LEFT`, `prg32.P2_A`, ... names
still parse but are being retired: the firmware no longer defines local
player-2 buttons, so C output that uses them does not compile (see
[Keeping the SDK and the Kit Aligned](#keeping-the-sdk-and-the-kit-aligned)). Numbers: `prg32.SCREEN_W` (320), `prg32.SCREEN_H` (200) and the other
`PRG32_` constants of [the C API](api.md) without the `PRG32_` start
(`prg32.TILE_W`, ...).

| Group | Functions |
|---|---|
| Drawing | `clear(color=BLACK)`, `rect(x, y, w, h, color=WHITE)`, `frame(x, y, w, h, color)`, `rect_rgb(x, y, w, h, rgb)`, `pixel(x, y, color)`, `line(x1, y1, x2, y2, color)`, `circle(x, y, r, color, filled=False)`, `bitmap(rows, x, y, color, scale=1)`, `sprite8(rows, x, y, fg, bg)`, `text(x, y, "TEXT", fg=WHITE, bg=BLACK)`, `number(value, x, y, fg, bg)`, `palette_set(index, color)`, `pixel_indexed(x, y, index)`, `rect_indexed(x, y, w, h, index)`, `clear_indexed(index)` |
| Tiles and playfields | `tile_define(id, rows, fg, bg)`, `playfield_clear(layer, id)`, `playfield_put(layer, tx, ty, id)`, `playfield_scroll(layer, x, y)`, `playfield_camera(x, y)`, `playfield_draw(layer, transparent=False)`, value `tile(layer, tx, ty)` |
| Sound | `note(note, voice=0, ms=250)` (60 is middle C, voices 0-7), `note_off(voice)`, `volume(0..255)`, `beep(freq, ms)`, `soundtrack_stop()`, `sound_stop_all()` |
| Tests for `if` | `held(button)`, `pressed(button)` (only the frame it goes down), `touching(ax, ay, aw, ah, bx, by, bw, bh)` |
| Values | `clamp(v, low, high)`, `sign(x)`, `sin(degrees)` and `cos(degrees)` (times 256), `rgb(r, g, b)`, `random(low, high)` (both included), `frame()` (the frame count), `peers()`, `peer_x(n)`, `peer_y(n)`, `peer_look(n)` |
| Game services | `band("TEXT", value)`, `score_submit(value)`, `scoreboard("TITLE")`, `splash("TITLE", "SUBTITLE", ms, bg, fg, accent)`, `player_name()`, `mp_join()`, `mp_share(x, y, look)` |

- A picture (`rows`) is written in place as 8 strings of up to 8 characters,
  `.` for an empty pixel: `["..XX....", ".XXXX...", ...]`.
- Text uses the PRG32 8x8 font, up to 64 characters (`band` 24, titles 32).
- `held`, `pressed` and `touching` are used as the whole `if` test or joined
  with `and`. **Cartridge rule:** such an `if` cannot have `elif`/`else` yet;
  write a second `if`.
- `sound_effect("name")` and `soundtrack_play("name")` name sounds made in the
  kit's AudioStudio, which is part of a kit project. The SDK has no AudioStudio
  document, so it refuses these two calls; use `note`, `beep`, or build in the
  kit. A packed AUDIO block can still be attached with `build --audio-block`.

## Limits

| Limit | Value |
|---|---|
| size of game.py | 64 KiB, 4000 lines |
| brackets inside each other | 32 |
| blocks (`if`/`for`) inside each other | 12 |
| functions / variables | 64 / 256 |
| helper calls inside each other | 8 |
| one expression after helpers are copied in | 200 characters |
| statements after helpers are copied in | 4000 |

The limits keep the compiler fast and safe on any input. The cartridge itself
is bounded by the [cartridge RAM profile](../usage/profiles.md#matching-host-tools-to-the-profile).

## Reading the Generated Code

The generated C has five parts: constants, the kit helper functions the game
uses (lists with bounds checks, `prg32_kit_div`, the sine table, ...), 8x8
pictures, the game state, and the three lifecycle functions. Above every
statement a comment repeats the Python step in words; beside many lines a
second comment explains the C. The assembly is a second view of the same game:
each Python step shows the C line it implements and every instruction is
commented. Comparing `game.py`, `game.c` and `game.S` is the bridge between the
Python, C and assembly tracks; see
[Teaching with PRG32](../learn/teaching_with_prg32.md#python-programming-track).

The same `game.py` always produces byte-identical C and assembly. To debug from
the SDK, run the cartridge in PRG32-QT, pause and step it with
`python3 -m prg32 debug` ([debugger API](api.md#prg32-qt-optional-debugger-api)),
and follow along in the generated `game.S`. Breakpoints on `game.py` lines are
available only in the kit's IDE, which keeps source maps.

## Keeping the SDK and the Kit Aligned

`prg32/python/` holds copies of the kit's `expressions.py`, `generator.py`,
`asm_generator.py` and `python_project.py` (renamed `compiler.py`), taken from
PRG32-Construction-Kit revision `0b9f1c0`, plus the part of `messages.py` the
compiler needs. The copies differ from the kit only on lines marked `SDK:`: the
generated files name `GENERATED_BY` instead of `PRG32-Construction-Kit`.
Problem codes and message keys are the kit's, so tools can translate them.

`tests/test_prg32_python.py` checks the language rules, the CLI and real
cartridge builds (when the toolchain is installed), and compares the SDK's C
and assembly with the kit's when a kit checkout is found beside this
repository or in `PRG32_CONSTRUCTION_KIT`. When the kit's language changes,
copy the four modules again, re-apply the `SDK:` lines, update the revision
above and in each file, and run that test.

**Pending kit change (2026-10-30).** The firmware added buttons C and D
(`PRG32_BTN_C` = bit 7, `PRG32_BTN_D` = bit 8) and removed the local player-2
bits `PRG32_P2_BTN_*` (multiplayer is online only). In PRG32-Construction-Kit,
remove the `P2_*` button names and constants, add `C` and `D`, then copy the
modules here. Until then the SDK still accepts `prg32.P2_*`, and its constant
value for `P2_LEFT` equals button D's bit.

## Validation Record

2026-10-29, macOS, Python 3.12, ESP-IDF toolchain `riscv32-esp-elf-gcc`:

- `tests/test_prg32_python.py` passed, including byte-identical C and assembly
  against the kit at `0b9f1c0` for the starter, the Python Pong example and a
  program that calls every `prg32` function, and cartridge builds of that
  program from both C and assembly.
- `examples/games/pong/python/game.py` was built from C (3064 bytes of code)
  and from assembly (3920 bytes), staged into cart0 of a scratch copy of
  `build-qemu/qemu_flash.bin`, started from the setup menu, and played on QEMU
  (`-display sdl`): both showed the moving ball and the computer paddle.
- The QEMU and ESP32-C6 (`breadboard1`) firmware built unchanged. No board was
  flashed and no physical cartridge slot was written.
