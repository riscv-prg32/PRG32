# PRG32 Controller Support

PRG32 uses one local digital joystick on the ESP32-C6 board. The input API keeps
the small register-like button mask used by the teaching examples:

| PRG32 bit | Meaning |
|---|---|
| `PRG32_BTN_LEFT` | Move left |
| `PRG32_BTN_RIGHT` | Move right |
| `PRG32_BTN_UP` | Move up |
| `PRG32_BTN_DOWN` | Move down |
| `PRG32_BTN_A` | Primary action |
| `PRG32_BTN_B` | Back / secondary action |
| `PRG32_BTN_SELECT` / `PRG32_BTN_START` | Select / pause |

The firmware no longer supports a UART controller bridge or a second local
digital joystick. `prg32_input_read_player(1)` returns the local joystick mask,
and `prg32_input_read_player(2)` returns `0` for source compatibility.

Multiplayer cartridges should exchange remote player input through the PRG32
multiplayer API instead of wiring a second local controller.
