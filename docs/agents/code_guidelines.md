# Code Guidelines

## Naming Rules for Code
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

## C Framework Guidelines

Please refer to the developer documentation located in for detailed C framework guidelines, implementation details, and rendering behavior. Agents MUST read these files when modifying the framework code.
