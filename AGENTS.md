# AGENTS.md

Guidance for coding agents working in this repository.

## Scope

This file applies to the whole repository. Follow it unless a more specific
`AGENTS.md` exists deeper in the tree.

PRG32 is an educational ESP32-C6 / RISC-V assembly gaming platform. The project
is deliberately simple, readable, and classroom-oriented. Preserve that spirit:
prefer explicit code, clear names, and teachable examples over clever compact
abstractions.

## Academic Project Context

Treat this repository as an academic-style software artifact:

- prioritize reproducibility over convenience
- keep implementation choices explainable in lab/classroom settings
- keep documentation aligned with coursework and assessment usage
- prefer transparent behavior over hidden automation

Named contributor metadata used across project docs:

- Raffaele Montella - UniParthenope - academic supervisor / project lead
- Ivan Cafiero - UniParthenope - Computer Science student

## Project Snapshot

- Target board: ESP32-C6.
- Display: ILI9341 SPI TFT on hardware, QEMU RGB panel on desktop, 320x240
  physical screen with a centered 320x200 game viewport.
- Framework component: `components/prg32`.
- Default firmware app: `main`, the resident PRG32 runtime, setup menu, and
  cartridge launcher.
- Game examples in RISC-V assembly and C: `examples/games`.
- Focused firmware feature demos: `examples/features`.
- Course materials: `docs`, especially `docs/labs`.
- Optional score server: `riscv-prg32/ScoreServer`.
- Standalone metrics server and streaming export tooling:
  `riscv-prg32/MetricsServer`.
- In-tree setup performance report tooling: `tools/prg32_metrics_paper.py`.
- Uploadable game cartridge tool: `tools/prg32_game.py`.
- Media conversion tools: `tools/prg32_image_convert.py`,
  `tools/prg32_image_prepare.py`, and `tools/prg32_audio_convert.py`.
- Student VS Code setup: `.vscode` and `PRG32.code-workspace`.
- Hardware notes and v2 scaffold: `hardware`.

## Repository Layout

```text
.
|-- components/prg32/              ESP-IDF component implementing the PRG32 API
|   |-- include/prg32.h             Public C/assembly ABI
|   |-- prg32_*.c                   Framework implementation files
|   |-- Kconfig                     Display backend selection
|   |-- idf_component.yml           External component dependencies
|   `-- CMakeLists.txt              Component registration
|-- main/                           Minimal firmware app and config
|   |-- main.c                      Resident runtime app
|   |-- prg32_config.h              Board pins and feature flags
|   `-- CMakeLists.txt
|-- partitions_prg32.csv            Resident firmware + cartridge slots
|-- examples/games/                 External assembly/C demos, not default build
|-- examples/features/              Focused rendering/firmware feature demos
|-- docs/                           Manuals, tutorial, labs, debugging exercises
|-- docs/cartridges.md              Flash-once uploadable game workflow
|-- docs/qemu.md                    QEMU virtual screen workflow
|-- hardware/                       Wiring, Wi-Fi, PCB/enclosure scaffold
|-- tools/qemu.sh                   macOS/Linux QEMU screen shortcut
|-- tools/qemu.ps1                  Windows PowerShell QEMU screen shortcut
|-- tools/prg32_game.py             Cartridge build/upload/staging tool
|-- tools/prg32_image_convert.py    Image/GIF/sprite/tile conversion
|-- tools/prg32_audio_convert.py    WAV/MIDI conversion
|-- .vscode/                        Student-ready VS Code tasks/settings
`-- PRG32.code-workspace
```

Do not reintroduce the old `components/urg32`, `main/urg32_config.h`, or top-level
`games/` layout. The canonical names are `prg32` and `PRG32`.

## Naming Rules

- Public C functions use `prg32_` prefix.
- Public macros and constants use `PRG32_` prefix.
- Component files use `components/prg32/prg32_*.c`.
- Include the framework as `#include "prg32.h"`.
- Include board configuration as `#include "prg32_config.h"`.
- Example game symbols should be unique and game-specific, for example:
  - `asteroids_graphics_init`
  - `asteroids_graphics_update`
  - `asteroids_graphics_draw`

Avoid generic `game_init`, `game_update`, and `game_draw` symbols in examples
unless a lab explicitly asks students to create a wrapper.

## Build Model

The default app is intentionally small and should remain a resident runtime
wrapper rather than embedding an example game:

```c
#include "prg32.h"

void app_main(void) {
    prg32_init();
    while (1) {
        if (prg32_cart_is_loaded()) {
            prg32_cart_call_update();
            prg32_cart_call_draw();
            prg32_gfx_present();
        }
    }
}
```

The default `main/CMakeLists.txt` should keep games out of the app:

```cmake
idf_component_register(
    SRCS "main.c"
    REQUIRES prg32
    INCLUDE_DIRS "."
)
```

Labs may temporarily add a specific example game source to `main/CMakeLists.txt`,
but the committed default should stay decoupled unless the user explicitly asks
to turn a game into the default firmware app.

## ESP-IDF Commands

Use these when ESP-IDF is available in the shell:

```bash
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

For desktop screen testing, use the QEMU build directory and defaults file.
Espressif's maintained RISC-V QEMU graphics target is ESP32-C3, while the
physical PRG32 board build remains ESP32-C6.

```bash
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

Useful checks:

```bash
git diff --check
python3 -m py_compile tools/prg32_game.py
python3 -m py_compile tools/prg32_metrics_paper.py
```

This environment may not have `idf.py`, `IDF_PATH`, or `riscv32-esp-elf-gcc` on
`PATH`. If they are missing, say so clearly. Do not claim a full firmware build
was run unless `idf.py build` actually succeeded.

## C Framework Guidelines

The public ABI is `components/prg32/include/prg32.h`. Keep it small, stable, and
friendly to RISC-V assembly callers.

When editing framework code:

- Keep dependencies in `components/prg32/CMakeLists.txt`.
- Keep `REQUIRES` and `PRIV_REQUIRES` independent of `CONFIG_*` choices; ESP-IDF
  expands component requirements before configuration-dependent source choices.
- Keep app-specific pin and feature config in `main/prg32_config.h`.
- Preserve `prg32_init()` as the one-call framework initializer.
- Do not expose ESP-IDF-only types in the public ABI unless absolutely needed.
- Return simple `int` status codes for APIs called from assembly.
- Check pointer inputs in helpers that can be called from student code.
- Keep comments short and educational where they clarify hardware or ABI behavior.

Important implementation details:

- ILI9341 receives RGB565 bytes in wire order; do not regress byte swapping.
- QEMU RGB receives RGB565 framebuffer data directly; do not apply ILI9341 wire
  byte swapping in `prg32_display_qemu_rgb.c`.
- The game viewport is `PRG32_GAME_W` x `PRG32_GAME_H` = 320x200.
- Splash screens, setup, and framework-owned menus use the full
  `PRG32_LCD_W` x `PRG32_LCD_H` = 320x240 display. Game APIs stay centered in
  the 320x200 viewport; if no explicit band color is set, the top/bottom bands
  should match the latest game background clear color.
- Game and feature-demo splash/title screens should use
  `prg32_splash_show_game` / `prg32_splash_draw_game` or normal game drawing
  calls so they stay inside the 320x200 viewport. Reserve
  `prg32_splash_show` / `prg32_splash_draw` for framework-owned full-screen
  UI.
- The status-band ABI (`prg32_band_*`) renders optional FPS, Wi-Fi, game, debug,
  or custom text in the physical top/bottom bands without changing the 320x200
  game coordinate system.
- Physical builds default to `CONFIG_PRG32_DISPLAY_ILI9341`.
- QEMU builds use `sdkconfig.defaults.qemu` and `CONFIG_PRG32_DISPLAY_QEMU_RGB`.
- `main/prg32_config.h` disables physical pins, buzzer, and real Wi-Fi
  multiplayer transport when the QEMU display backend is selected.
- `components/prg32/idf_component.yml` and CMake only pull
  `esp_lcd_qemu_rgb` for the ESP32-C3 emulator target.
- Console text must remain visible on LCD and UART mirror modes.
- `prg32_input_read()` merges local GPIO buttons, QEMU keyboard input, and
  diagnostic input state.
- Uploadable cartridges run from `prg32_cart_exec` and are linked for that
  runtime address. Rebuild cartridges whenever the resident firmware changes.
- Keep `PRG32_CART_RAM_SIZE` small enough for classroom examples unless the
  partition/RAM plan is intentionally revised.
- Keep `prg32_metrics_record()` non-blocking. Metrics collection must copy into
  a small queue and leave HTTP upload to the background task so profiling does
  not become the main source of frame-time jitter.
- Keep `partitions_prg32.csv`, `sdkconfig.defaults`, and `sdkconfig.defaults.qemu`
  in sync when changing cartridge slots.
- `prg32_input_read_player(2)` returns `0`; multiplayer games should use the
  cartridge multiplayer API for remote player snapshots.
- Cartridge multiplayer groups peers by cartridge signature and uses ESP32-C6
  Wi-Fi station mode plus WebSocket to a Node.js `ws` server.
- Joystick text input lives in `prg32_keyboard.c` and should stay usable from
  games as well as setup screens.
- Setup mode is entered by holding A+B at boot, by holding `PRG32_PIN_SETUP`
  low when that optional pin is wired, when no cartridge is present, or when
  multiple cartridges exist without a saved default cartridge.
- Keep `prg32_device_demo_run()` current whenever framework capabilities are
  added or changed. The setup-launched device demo should remain a quick
  hardware/classroom smoke test covering display, input, audio, Wi-Fi status,
  cartridge state, sprites, scrolling/playfields, status bands, RGB LED/audio
  VU behavior, arcade-inspired viewport sketches, the tile-engine platformer,
  the fixed-point raycaster, the dual-playfield space cockpit, and any new
  framework feature.

## Assembly Example Guidelines

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

## Feature Demo Guidelines

Focused demos live in `examples/features/<name>/`.

Use them for framework mechanics that are bigger than one API call but smaller
than a full game:

- scrolling and parallax playfields
- animated sprites
- dual playfields

Each feature demo should contain a `README.md`, one assembly source such as
`demo.S`, and a C source under `c/demo.c` when it belongs to the standard
student demo set. Export game-like symbols with a feature-specific prefix:

```text
<feature>_init
<feature>_update
<feature>_draw
```

Use the `_c` suffix for C demo prefixes, for example `dual_playfield_c_init`.

Keep feature demos shorter than games. They should isolate the API behavior
students are meant to inspect.

## Documentation Guidelines

Docs are part of the teaching product. Keep them current when changing APIs,
paths, or example names.

Core docs:

- `README.md`: top-level orientation.
- `docs/tutorial.md`: end-to-end tutorial.
- `docs/tutorial_c_game.md`: C programming tutorial.
- `docs/framework_manual.md`: PRG32 API and ABI overview.
- `docs/qemu.md`: macOS, Windows, and Linux virtual screen workflow.
- `docs/cartridges.md`: flash-once cartridge upload workflow.
- `docs/score_api.md`: board-local and external ScoreServer APIs.
- `https://github.com/riscv-prg32/ScoreServer`: Flask + SQLite classroom score
  server for persistent score storage.
- `https://github.com/riscv-prg32/MultiplayerServer`: Node.js WebSocket relay
  for cartridge multiplayer.
- `docs/labs/`: lab handouts, debugging exercises, break/fix assignments.
- `CONTRIBUTORS.md`: contributor metadata for coursework/project submissions.
- `CITATION.cff`: citation metadata for reports, theses, and reproducible references.

When editing labs:

- Use concrete student actions.
- Include a visible checkpoint.
- Include a short reflection/debugging question.
- Keep commands copy-pastable.

## VS Code Student Setup

Keep `.vscode` and `PRG32.code-workspace` plug-and-play for students.

Recommended extension list should include:

- Espressif ESP-IDF extension.
- Microsoft C/C++ extension.
- Python extension for the external ScoreServer repository.

Tasks should remain simple wrappers around:

- `idf.py set-target esp32c6`
- `idf.py menuconfig`
- `idf.py build`
- `idf.py flash monitor`
- `idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor`
- `idf.py -B build-qemu gdb`
- `tools/qemu.sh` and `tools/qemu.ps1`
- `python3 tools/prg32_game.py build ...`
- `python3 tools/prg32_game.py upload ...`
- `python3 tools/prg32_game.py upload-qemu ...`

Do not hard-code one instructor machine path. Use workspace-relative paths and
standard ESP-IDF configuration variables where possible.

## Score Server Guidelines

The score server lives in `https://github.com/riscv-prg32/ScoreServer`, not in
this firmware repository. Do not reintroduce `tools/prg32_score_server`.

- Keep the board firmware compatible with the external server endpoints:
  - `GET /api/scores`
  - `POST /api/scores`
- Update `docs/score_api.md` whenever the external server workflow or endpoint
  contract changes.

## Hardware Guidelines

Hardware docs should reinforce the architecture:

```text
ESP32-C6 -> Wi-Fi station -> Node.js WebSocket relay -> matching cartridge peers
```

ESP32-C6 is the main RISC-V teaching MCU. Do not document it as a general USB HID
host for arbitrary gamepads. Multiplayer uses the ESP32-C6 native Wi-Fi radio.

## Git and Workspace Rules

- The user may have local changes. Do not revert files you did not change unless
  explicitly asked.
- Treat untracked files as user-owned unless you created them during the current
  task.
- Do not commit or push unless the user asks.
- When committing, use a short, specific message.
- Before committing or finalizing, run at least `git diff --check`.
- If a push is rejected because remote moved, pull/rebase and resolve conflicts
  without force-pushing unless the user explicitly requests a force push.

## Metadata Consistency Rules

When changing authorship or academic attribution, keep these files in sync:

- `README.md`
- `CONTRIBUTORS.md`
- `CONTRIBUTING.md`
- `CITATION.cff`

Do not silently remove existing contributor attribution unless explicitly requested.

## Validation Checklist

Before reporting completion, try the relevant subset:

```bash
git diff --check
python3 -m py_compile tools/prg32_game.py
python3 -m py_compile tools/prg32_metrics_paper.py
idf.py build
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu build
```

If ESP-IDF is not available, use the first two checks and clearly state that the
firmware or QEMU build could not be run locally.

For assembly-only example changes, also inspect:

```bash
rg -n "^[0-9]{2,}:|[0-9]{2}f|[0-9]{2}b" examples/games/<name>
```

This catches accidental multi-digit numeric labels in classroom assembly.
