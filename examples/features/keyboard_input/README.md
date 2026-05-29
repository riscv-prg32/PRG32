# Joystick Keyboard Demo

This demo exposes PRG32 alphanumeric input through a joystick-controlled
on-screen keyboard.

- D-pad: move between keys.
- SELECT: select the highlighted key.
- A: escape to the previous state.
- B: confirm, like selecting the on-screen `return` key.
- On-screen keys include `delete`, `shift`, `return`, and an `ascii` page key.
- The ASCII page supports printable ASCII characters from 0x20 through 0x7e.
- The demo uses `prg32_input_read_menu()`, so setup screens and games share the
  same local joystick controls.

Use entry prefix `keyboard_input_c`.
