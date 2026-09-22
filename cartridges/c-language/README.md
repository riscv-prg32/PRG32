# PRG32 C

PRG32 C turns the console into a small C programming environment: pair a
Bluetooth keyboard, type a program in the syntax-coloured editor, press **F5**
and the program runs on the PRG32 screen. On QEMU the terminal running the
emulator acts as the keyboard.

The cartridge is one portable C file, [`src/c_language.c`](src/c_language.c),
organised as a classic language system:

| Part | Functions | What it teaches |
|---|---|---|
| Editor | `ed_*`, `hl_line` | text buffers, cursor movement, syntax colouring |
| Lexer | `next`, `lex_*` | turning characters into tokens |
| Compiler | `expr`, `stmt`, `global_decl` | one-pass recursive descent and precedence climbing that emits stack-machine code, in the spirit of R. Swierczek's *c4* |
| Virtual machine | `vm_run_batch`, `vm_syscall` | fetch/decode/execute, call frames, system calls |

C pointers are byte offsets inside the private 10 KiB array `vm_mem[]`; every
load, store and stack push is bounds-checked. Faulty programs stop with a
message such as `LINE 3: NULL pointer access` and the editor jumps to that
line, while the console keeps running. The VM runs for about 12 ms per frame
and pauses inside `getchar()`, `readline()`, `sleep()` and `frame()`, so
interactive programs and animations do not block the PRG32 main loop.

## Keys

| Key | Action |
|---|---|
| F5 / CTRL+R | compile and run |
| ESC / CTRL+C (joystick B) | stop the running program |
| F3 / CTRL+E (joystick SELECT) | load the next example |
| F2 / CTRL+N | new program |
| F1 | help screen with the built-in functions |
| arrows, HOME, END, PGUP, PGDN | move the cursor |
| joystick A | run (useful without a keyboard) |

F2 and F3 ask for a second press when the program has unsaved changes.
Programs live in RAM only: they are lost on reset.

The cartridge calls `prg32_btkbd_set_mapping(0)` at start-up so typed letters
do not also act as joystick buttons; the firmware restores the mapping for the
next cartridge. See the [Bluetooth keyboard guide](../../docs/hardware/bluetooth_keyboard.md).

## Language subset

Supported: `int`, `char` (unsigned, as on RISC-V), pointers at any level,
one-dimensional arrays (global arrays accept `{...}` and `"string"`
initializers), functions with parameters, recursion and prototypes, `if`,
`else`, `while`, `do`/`while`, `for` (also `for (int i = ...)`), `break`,
`continue`, `return`, all C operators including `?:`, `&&`/`||`
short-circuit, compound assignment and pointer arithmetic, `sizeof(type)`,
casts, `enum`, `#define NAME constant`, string/character escapes, `//` and
`/* */` comments. `#include` lines are ignored; `const`, `static`,
`unsigned`, `signed` are accepted and ignored; `long`/`short` mean `int`.

Not supported: `struct`, `union`, `switch`, `float`, `goto`, multi-dimensional
arrays, local array initializers, function pointers. Local variables are
visible from their declaration to the end of the function (not only to the end
of their block).

Limits: 5 KiB of source, 2560 code words, 10 KiB of VM memory for globals,
strings, `malloc` and the stack, and 20 levels of nested expressions and
statements.

## Built-in functions

| Group | Functions |
|---|---|
| Text output | `putchar(c)`, `puts(s)`, `printf(fmt, ...)` with `%d %i %u %x %X %c %s %%`, width, `-` and `0`; `cls()`, `gotoxy(x, y)`, `textcolor(0..15)` |
| Keyboard | `getchar()` (waits, echoes), `getkey()` (0 if none), `readline(buf, max)` (line editing), `keydown(k)` (held key, e.g. `keydown(KEY_LEFT)` or `keydown('a')`), `btn()` (joystick mask) |
| Strings and memory | `strlen`, `strcmp`, `strcpy`, `strcat`, `atoi`, `memset`, `malloc`, `free` (bump allocator: only the latest block is released) |
| Maths and time | `abs`, `rand()` (0..32767), `srand(seed)`, `ms()`, `sleep(ms)`, `exit(code)` |
| Graphics (320x200) | `clear(color)`, `pixel(x, y, c)`, `rect(x, y, w, h, c)`, `line(x0, y0, x1, y1, c)`, `text(x, y, s, c)`, `rgb(r, g, b)`, `frame()` (wait for the next frame) |
| Sound | `note(midi, ms)` |

Predefined constants: `KEY_UP KEY_DOWN KEY_LEFT KEY_RIGHT KEY_ENTER KEY_ESC
KEY_BACKSPACE KEY_TAB`, `BTN_LEFT ... BTN_SELECT`, `BLACK WHITE RED GREEN BLUE
YELLOW CYAN MAGENTA`, `NULL`. The first graphics call switches the screen from
the text console to the canvas; `cls()` switches back.

## Examples

F3 cycles through six examples: hello world with colours, recursive Fibonacci,
guess-the-number with `readline`, pointer-based string reversal, bubble sort on
an initialized global array, and a paddle-and-ball graphics game.

## Build and test

```bash
PRG32_ARCHITECTURE=esp32c6 cartridges/c-language/scripts/build.sh
PRG32_ARCHITECTURE=qemu cartridges/c-language/scripts/build.sh
cartridges/c-language/tests/check_source.sh
cartridges/c-language/tests/run_host_tests.sh
```

The host test compiles the interpreter for the development computer with small
stand-ins for the PRG32 calls and runs more than thirty programs through the
real compiler and VM (operators, loops, recursion, pointers, arrays, `printf`
formats, keyboard input, compile errors and run-time errors).

The package needs firmware with cartridge ABI 1.7 (feature `btkeyboard`) and
the default 64 KiB cartridge RAM profile (it uses about 61 KiB).
