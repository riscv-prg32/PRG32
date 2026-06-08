# Contributing to PRG32

PRG32 is an educational and academic-style project. Contributions should be
small, reproducible, clearly explained, and useful in a classroom setting.

This guide is written for students first, but the same rules apply to every
contributor.

## Student Workflow

1. Pick one clear goal.
2. Create a branch from `main`.
3. Make the smallest change that proves the idea.
4. Update docs or labs when behavior, commands, paths, or APIs change.
5. Run the validation checklist.
6. Open a pull request with a short reflection on what you learned.

Branch name examples:

```bash
git checkout -b lab/input-debug-overlay
git checkout -b game/asteroids-collision
git checkout -b docs/cartridge-workflow-notes
```

## Academic Contribution Format

Use this structure in pull request descriptions and course submissions:

```text
Context:
- What problem or learning objective does this change address?

Method:
- What files changed?
- What design choices or constraints mattered?

Validation:
- What commands did you run?
- What did you observe on QEMU or hardware?

Reflection:
- What limitation remains?
- What would you improve next?
```

## Local Setup

Recommended setup:

1. Open `PRG32.code-workspace` in VS Code.
2. Install the recommended extensions.
3. Install and source ESP-IDF for ESP32-C3 and ESP32-C6.
4. Use QEMU for early graphics/debugging work.
5. Use the ESP32-C6 board for final hardware validation.

ESP-IDF commands:

```bash
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

QEMU commands:

```bash
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu set-target esp32c3
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu build
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

PlatformIO users can open the repository root and use the `prg32-esp32c6`
environment for physical board builds.

## Validation Checklist

Run these checks before opening a pull request:

```bash
git diff --check
PYTHONPYCACHEPREFIX=/tmp/prg32-pycache python3 -m py_compile \
  tools/prg32_game.py \
  tools/prg32_metrics_paper.py
PYTHONPYCACHEPREFIX=/tmp/prg32-pycache python3 -m unittest discover -s tests
python3 tools/prg32_game.py doctor --host-only
```

When ESP-IDF is available, also run:

```bash
idf.py build
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu build
```

For end-to-end QEMU cartridge testing:

```bash
./scripts/smoke_test.sh
```

If a tool is missing, write that clearly in the pull request. Do not claim a
firmware or QEMU build passed unless it actually ran.

## Coding Guidelines

- Keep the educational style: explicit names, simple control flow, and readable
  examples are better than clever abstractions.
- Preserve public naming: C functions use `prg32_*`, constants use `PRG32_*`.
- Keep `main` as a minimal resident firmware app unless the task explicitly
  changes the default firmware behavior.
- Keep example games in `examples/games`; do not move them into the default app.
- Keep focused rendering demonstrations in `examples/features`.
- Do not reintroduce legacy `urg32` paths or symbols.
- Keep generated files, local databases, build outputs, and `.prg32` cartridges
  out of commits.
- Keep docs in sync when commands, paths, APIs, examples, or lab flows change.

## Assembly Guidelines

Assembly examples are teaching material. They should be verbose enough that a
new student can trace them by hand.

- Start RISC-V example files with `.option norelax`.
- Export game-specific symbols such as `pong_graphics_init`.
- Save and restore `ra` before calling C helpers.
- Keep the stack 16-byte aligned around C calls.
- Use `t0` to `t6` for temporary values that do not survive C calls.
- Save and restore `s0` to `s11` if you use them.
- Avoid assuming `a` or `t` registers survive a function call.

## C Example Guidelines

C examples are teaching material for programming classes. Keep them direct,
small, and comparable with the matching assembly examples.

- Export `<name>_c_init`, `<name>_c_update`, and `<name>_c_draw`.
- Keep state in simple globals or small structs.
- Prefer the PRG32 API in `prg32.h` over standard-library calls.
- Avoid heap allocation in examples unless a lab explicitly teaches it.
- Update `examples/games/README.md` when adding or renaming C examples.

## Pull Request Process

1. Keep the pull request focused.
2. Explain the student-visible result.
3. Include validation commands and outcomes.
4. Add screenshots for display or graphics changes.
5. Request review only after the local checklist is done.

Good pull request title examples:

- `Add register tracing lab checkpoint`
- `Fix cartridge partition parsing test`
- `Document PlatformIO ESP32-C6 setup`

## Academic Integrity

Students may use PRG32 examples, course notes, and instructor feedback, but they
must understand and explain the submitted work. If AI tools or external code
were used, note that in the reflection and describe what was changed.

## Contributor Attribution

Keep contributor names consistent across project metadata files. If you add a
contributor, update:

- `CONTRIBUTORS.md`
- the README contributor section
- `CITATION.cff` when authorship metadata changes

Current named contributors:

- Raffaele Montella - UniParthenope - academic supervisor / project lead
- Ivan Cafiero - UniParthenope - Computer Science student
- Simone Boscaglia - Uniparthenope - Computer Science student