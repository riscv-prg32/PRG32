# Tutorial: Writing a PRG32 Game in Python

This tutorial writes a PRG32 cartridge in PRG32 Python, a small typed part of
Python. The SDK translates `game.py` into commented C and RISC-V assembly and
builds a normal portable cartridge from it, so the game runs on QEMU, on the
ESP32-C6 and in PRG32-QT exactly like the C and assembly examples. The language
reference is [PRG32 Python](../software/python_language.md).

## Learning Goals

- Split a program into `init`, `update` and `draw` functions.
- Declare typed variables (`int`, `bool`, `list[int]`) and use `global`.
- Read buttons and test rectangle collisions.
- Explain where PRG32 Python differs from desktop Python, and why.
- Read the C and assembly generated from your own Python.

## 1. Create and Check a Game

```bash
python3 -m prg32 python new build/mygame/game.py
python3 -m prg32 python check build/mygame/game.py
```

Open `build/mygame/game.py`. It imports `prg32` and defines the three
lifecycle functions. The firmware calls `init` once, then `update` and `draw`
once per frame.

Checkpoint: `check` prints `is valid prg32-python-1`.

## 2. Build and Run It

Build the cartridge (this needs the RISC-V toolchain, so first run
`source $HOME/esp-idf/export.sh`):

```bash
python3 -m prg32 python build build/mygame/game.py --name mygame --out build/mygame/mygame.prg32
```

Run it on QEMU:

```bash
python3 -m prg32 qemu build
python3 -m prg32 qemu upload build/mygame/mygame.prg32
python3 -m prg32 qemu run
```

Or upload it to the board:

```bash
python3 -m prg32 esp32c6 upload build/mygame/mygame.prg32 --url http://192.168.4.1
```

Checkpoint: a yellow square moves left and right with the joystick.

## 3. Read Input and Move

The starter already declares `player_y: int = 180`. Make UP and DOWN move the
square by extending `update`:

```python
def update() -> None:
    global player_x, player_y
    # ... the LEFT and RIGHT lines of the starter stay here ...
    if prg32.held(prg32.UP):
        player_y -= 3
    if prg32.held(prg32.DOWN):
        player_y += 3
    player_y = prg32.clamp(player_y, 0, prg32.SCREEN_H - 16)
```

`global` says that `update` changes the module variable instead of creating a
new local one, exactly as in desktop Python.

Reflection question: what does `prg32.held` become in `build/mygame/mygame.c`?
Find the `prg32_input_read()` call and the `&` with a `PRG32_BTN_` mask.

## 4. Discover the Cartridge Rules

Add this line to `update`, then run `check`:

```python
player_x = player_x / 2
```

The checker refuses `/`, because `/` makes a fraction and every PRG32 number
is a whole 32-bit number (the RV32IM core has no floating point). Change it to
`player_x // 2`. Now try:

```python
while player_x > 100:
    player_x -= 1
```

The checker refuses `while`, because a cartridge must never stop the frame
loop. Rewrite it with `for i in range(...)`, which runs at most 1024 rounds.

Reflection question: desktop Python says `-7 // 2` is `-4`. PRG32 Python says
`-3`. Which RISC-V instruction decides that, and why does the cartridge follow
the processor instead of desktop Python?

## 5. Compare Python, C and Assembly

```bash
python3 -m prg32 python to-c build/mygame/game.py --out build/mygame/game.c
python3 -m prg32 python to-asm build/mygame/game.py --out build/mygame/game.S
```

Put `game.py`, `game.c` and `game.S` side by side. Every Python step appears in
the C as a comment followed by the C statement, and in the assembly as a
comment with the C line followed by commented instructions.

Checkpoint: find `player_x -= 3` in all three files. The comments call it the
step "change player_x by -3", and the C statement is `player_x += -3;`.

## 6. A Complete Example: Pong

`examples/games/pong/python/game.py` is the same game as
`examples/games/pong/c/game.c`, statement by statement:

```bash
python3 -m prg32 python build examples/games/pong/python/game.py \
  --entry-prefix pong_python --name pong-python \
  --out build-esp32c6/pong-python.prg32
```

The Python `prg32.touching(...)` is the C `prg32_sprite_hitbox(...)`, and
`prg32.clamp(...)` replaces the two `if` statements that keep the paddle on the
screen.

## Break and Fix Exercise

Break it:

1. In the Pong `update`, replace `ball_dx = -ball_dx` with `ball_dx = ball_dx`.
2. Build and run.
3. Watch the ball leave the screen on the right.

Fix it:

1. Restore the line.
2. Explain which condition detects the screen edge.
3. Find the same test in the generated `pong-python.c` and in the C example.

## Trainer Notes

- Use PRG32 Python for students who know Python and are not ready for C. The
  generated C is the bridge to the [C tutorial](tutorial_c_game.md), the
  generated assembly to the [assembly tutorial](tutorial_graphic_game.md).
- The checker refuses unsupported Python with the line and a suggestion, so
  the error messages are part of the lesson.
- Students who use the PRG32-Construction-Kit IDE write the same `game.py`;
  the SDK and the kit produce byte-identical C and assembly from it.
