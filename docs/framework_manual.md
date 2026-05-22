# PRG32 Framework Manual

PRG32 lets students write game logic in RISC-V assembly or C while a small
framework provides hardware access.

## Assembly ABI

Arguments use the standard RISC-V calling convention: `a0` to `a7` carry
arguments and return values, `ra` holds the return address, and `sp` is kept
16-byte aligned around C calls.

## Console modes

- `PRG32_MODE_UART_ONLY`: serial terminal only.
- `PRG32_MODE_LCD_ONLY`: text appears on the ILI9341 display.
- `PRG32_MODE_UART_LCD_MIRROR`: debug text is sent both to serial and LCD.

## Input and Player 2

`prg32_input_read()` returns the full PRG32 input register. Player 1 uses the
low bits:

- `PRG32_BTN_LEFT`
- `PRG32_BTN_RIGHT`
- `PRG32_BTN_UP`
- `PRG32_BTN_DOWN`
- `PRG32_BTN_A`
- `PRG32_BTN_B`
- `PRG32_BTN_SELECT` (`PRG32_BTN_START` is kept as an alias)

Player 2 uses the same layout shifted into the `PRG32_P2_*` bits. Games can use
`prg32_input_read_player(1)` or `prg32_input_read_player(2)` to get a normalized
low-bit mask for each player. This keeps Pong-style two-player games readable
in both C and assembly.

QEMU and host-driven tests can inject the same bitmask through
`prg32_diag_set_input_state()`, so two-player logic can be tested without the
physical second joystick mounted.

Menu/setup helpers can call `prg32_input_read_menu()` to merge joystick 1 and
joystick 2 into the same normalized low-bit mask.

System hotkey:

- A + B + DOWN on joystick 1 or joystick 2: restart the ESP32-C6 firmware from
  anywhere in the PRG32 input path.

## Joystick Text Input

The on-screen keyboard lets games and framework setup screens collect short
alphanumeric text without a USB keyboard.

Useful calls:

- `prg32_keyboard_init(keyboard, buffer, capacity)`
- `prg32_keyboard_update(keyboard, input_mask)`
- `prg32_keyboard_draw(keyboard, x, y)`
- `prg32_text_input(buffer, capacity, title)`

Controls:

- D-pad: move around the key grid.
- SELECT: select the highlighted on-screen key.
- A: escape back to the previous state.
- B: confirm, equivalent to selecting the on-screen `return` key.

The keyboard uses a QWERTY layout with explicit `delete`, `shift`, and `return`
keys. `shift` toggles between lower-case and upper-case/symbol labels. The
`ascii` key opens a printable ASCII page covering characters 0x20 through 0x7e.
The LCD and QEMU text renderers include distinct glyphs for the full printable
ASCII range; console output also treats tab and DEL/backspace as text controls.

## Graphics model

The physical display is 320x240, while the normal game viewport remains
320x200. The firmware splash, setup, Wi-Fi setup, developer menu, and about
screen use the full display. Game and feature-demo drawing calls use the
centered 320x200 viewport so cartridges keep the same coordinate system and
the retro frame. Unless a program sets a band color explicitly, the upper and
lower horizontal bands are filled with the same color passed to
`prg32_gfx_clear`.

The QEMU renderer exposes the same 320x240 physical screen and centers the
320x200 PRG32 game viewport inside it. Student assembly code does not change;
only the selected display backend changes.

For classroom debugging, optional helper `prg32_debug_overlay_draw` can print
`x`, `y`, input mask, frame, and tick info on the top scanline.

Display backend selection:

- `CONFIG_PRG32_DISPLAY_ILI9341`: physical ILI9341 SPI TFT, default.
- `CONFIG_PRG32_DISPLAY_QEMU_RGB`: QEMU virtual RGB framebuffer.

Use the QEMU defaults file when running on a desktop:

```bash
idf.py -B build-qemu -D SDKCONFIG=build-qemu/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.qemu qemu --graphics monitor
```

When the QEMU backend is selected, `main/prg32_config.h` disables physical GPIO
buttons, the buzzer, and the UART controller bridge. This keeps desktop screen
tests focused on rendering and GDB exercises.

## Splash Screens

The resident firmware shows the PRG32 logo image after display initialization.
It can be disabled or timed through Kconfig:

- `CONFIG_PRG32_SPLASH_ENABLED`
- `CONFIG_PRG32_SPLASH_DURATION_MS`
- `CONFIG_PRG32_SPLASH_SOUND_ENABLED`

When splash sound is enabled, firmware plays a short welcome phrase through the
I2S audio subsystem only when the configured audio pins do not conflict with the
reference display/input wiring. Otherwise it uses the passive buzzer when one
is configured.

Graphic games can reuse the 320x200 game splash helpers:

- `prg32_splash_show_game(title, subtitle, duration_ms, bg, fg, accent)`:
  draw a game title screen, present it, and wait.
- `prg32_splash_draw_game(title, subtitle, bg, fg, accent)`: draw a game title
  screen without delaying, useful inside a title-state loop.

Framework-owned full-screen splash helpers remain available:

- `prg32_splash_show(title, subtitle, duration_ms, bg, fg, accent)`: draw,
  present, and wait on the full 320x240 display.
- `prg32_splash_draw(title, subtitle, bg, fg, accent)`: draw a splash/title
  screen without delaying on the full 320x240 display.
- `prg32_splash_show_default()`: show the firmware-style PRG32 splash.
- `prg32_gfx_set_fullscreen(enabled)`: switch between full-screen framework
  drawing and the centered game viewport.
- `prg32_gfx_set_band_color(color)`: set a custom color for the game viewport
  bands.
- `prg32_gfx_use_background_bands()`: return to automatic background-colored
  bands.

Assembly programs pass C strings in `a0` and `a1`, duration in `a2`, and RGB565
colors in `a3` to `a5`.

## Status Bands

When the viewport is active, PRG32 owns the 20-pixel band above the game and
the 20-pixel band below it. By default they follow the game background color.
Games can opt in to status text without changing the 320x200 play area:

- `prg32_band_set_mode(PRG32_BAND_TOP, mode)`
- `prg32_band_set_mode(PRG32_BAND_BOTTOM, mode)`
- `prg32_band_set_text(band, text)`
- `prg32_band_set_game_info(text)`
- `prg32_band_log(message)`
- `prg32_band_set_colors(band, fg, bg)`
- `prg32_band_use_default_colors(band)`

Available modes are `PRG32_BAND_MODE_NONE`, `PRG32_BAND_MODE_FPS`,
`PRG32_BAND_MODE_WIFI`, `PRG32_BAND_MODE_GAME`,
`PRG32_BAND_MODE_DEBUG`, and `PRG32_BAND_MODE_CUSTOM`. The setup developer
menu lets a trainer choose what appears in the top and bottom bands and stores
that choice in NVS.

## Cartridge runtime

The resident firmware includes a cartridge loader so games can be replaced
without reflashing the whole ESP32-C6 app.

Important constants:

- `PRG32_CART_MAGIC`: `.prg32` package magic.
- `PRG32_CART_ABI_MAJOR` / `PRG32_CART_ABI_MINOR`: loader ABI version.
- `PRG32_CART_RAM_SIZE`: executable cartridge RAM window, currently 32 KiB.
- `PRG32_CART_SLOT_COUNT`: number of persistent flash cartridge slots.

Important functions:

- `prg32_cart_load_addr()`: runtime address used by the host linker.
- `prg32_cart_install(image, size, persist)`: validate, load, and optionally store.
- `prg32_cart_install_slot(slot, image, size, persist)`: install to `cart0` or `cart1`.
- `prg32_cart_select_slot(slot)`: load a stored cartridge from one slot.
- `prg32_cart_default_slot()`: read the saved default boot cartridge.
- `prg32_cart_set_default_slot(slot)`: save a default slot, or pass `-1` to clear it.
- `prg32_cart_select_default()`: load the saved default slot.
- `prg32_cart_stored_count()`: count valid stored cartridges.
- `prg32_cart_get_slot_info(slot, info)`: inspect one persistent slot.
- `prg32_cart_call_init()`
- `prg32_cart_call_update()`
- `prg32_cart_call_draw()`

The default app automatically calls the current cartridge every frame when one
is loaded.

## Wi-Fi Modes and Setup

PRG32 supports three Wi-Fi runtime modes:

- `PRG32_WIFI_MODE_STA`: connect to an existing access point.
- `PRG32_WIFI_MODE_AP`: create the PRG32 access point.
- `PRG32_WIFI_MODE_APSTA`: keep the upload AP while also connecting as station.

After the startup splash, the resident ESP32-C6 firmware enters setup mode when
A and B are held during boot, whenever no stored cartridge is available, or
when multiple cartridges are available but no default cartridge has been saved.
If one cartridge is available, it starts automatically. If a default cartridge
has been saved, that cartridge starts automatically even when multiple slots are
filled.

The setup main menu contains cartridge launch, default cartridge selection,
Wi-Fi setup, audio setup, the developer band menu, the device demo, the about
screen, and exit. Use UP/DOWN to choose, SELECT or B to confirm, and A to cancel/back. The
device demo is a firmware-owned smoke test for display, input, audio, Wi-Fi
status, cartridges, sprites, scrolling, playfield rendering, status bands, and
small sketches inspired by Pong, Breakout, Space Invaders, Pacman, Tetris,
Pole Position, Asteroids, a tile-engine platformer, and a Doom-style
raycaster. It also includes a space-cockpit sketch where the starfield and
cockpit are separate playfields.

The Wi-Fi setup screen lets the user choose access-point mode or infrastructure
mode. Infrastructure mode scans for nearby SSIDs, lists them on screen, and
uses UP/DOWN plus SELECT or B to select. A cancels back. Joystick 1 and
joystick 2 work the same way in setup. The setup UI also shows the active Wi-Fi
mode and IP address; AP mode shows the AP SSID, while
infrastructure mode shows the selected/connected SSID.

`PRG32_BOOT_SETUP_MODE` in `main/prg32_config.h` can still force setup on every
boot for custom classroom images. If `PRG32_PIN_SETUP` is wired, holding it low
during boot also forces setup mode.

Useful calls:

- `prg32_wifi_setup_requested()`
- `prg32_wifi_setup_run()`
- `prg32_wifi_start_mode(config)`
- `prg32_wifi_current_mode()`
- `prg32_wifi_current_ip()`
- `prg32_wifi_current_ssid()`

The chosen settings are stored in NVS under the `prg32wifi` namespace. QEMU
builds keep physical Wi-Fi disabled by default, but games can still compile
against the same API and exercise setup screens.

## Tile engine

The tile engine exposes a 40x25 grid of 8x8 tiles. This matches a 320x200 retro
screen exactly.

Useful calls:

- `prg32_tile_define(id, bitmap8x8, fg, bg)`: define a reusable tile.
- `prg32_tile_put(tx, ty, id)`: place a tile in the simple 40x25 tile map.
- `prg32_tile_present()`: draw dirty simple-map tiles and present the frame.

The simple tile map is best for first tile exercises. Use playfields when the
lesson needs scrolling, parallax, or two layers.

## Scrolling and Playfields

PRG32 includes two scrollable 64x32 tile playfields. A playfield is larger than
the visible 40x25 tile viewport, so it can scroll horizontally and vertically.

Useful calls:

- `prg32_playfield_clear(layer, tile_id)`: fill one playfield with a tile.
- `prg32_playfield_put(layer, tx, ty, id)`: place a tile in one playfield.
- `prg32_playfield_scroll(layer, x, y)`: set pixel scroll for one layer.
- `prg32_playfield_scroll_by(layer, dx, dy)`: move one layer by a delta.
- `prg32_playfield_camera(x, y)`: set a shared camera position.
- `prg32_playfield_parallax(layer, x_q8, y_q8)`: set camera scale per layer.
- `prg32_playfield_draw(layer, transparent_zero)`: draw one layer.
- `prg32_playfield_draw_dual()`: draw layer 0 opaque and layer 1 transparent.

Parallax factors use Q8 fixed point:

```text
256 = 1.0x camera speed
128 = 0.5x camera speed
 64 = 0.25x camera speed
```

For a parallax background, set layer 0 to a smaller factor and layer 1 to
`PRG32_PARALLAX_1X`. The foreground layer treats tile `0` as transparent when
drawn through `prg32_playfield_draw_dual()`.

## Platform Tile Engine

The platform helpers build on playfields by assigning behavior flags to tile
IDs and moving an actor rectangle through the flagged world.

Tile flags:

- `PRG32_TILE_FLAG_SOLID`: blocks movement from every side.
- `PRG32_TILE_FLAG_PLATFORM`: one-way floor, useful for ledges.
- `PRG32_TILE_FLAG_HAZARD`: marks spikes, enemies, or damage tiles.
- `PRG32_TILE_FLAG_COLLECT`: marks collectible tiles.

Actor state bits:

- `PRG32_PLATFORM_ON_GROUND`
- `PRG32_PLATFORM_HIT_LEFT`
- `PRG32_PLATFORM_HIT_RIGHT`
- `PRG32_PLATFORM_HIT_HEAD`
- `PRG32_PLATFORM_HAZARD`
- `PRG32_PLATFORM_COLLECT`

Useful calls:

- `prg32_platform_tile_flags(tile_id, flags)`: define tile behavior.
- `prg32_platform_actor_init(actor, layer, x, y, w, h)`: create an actor.
- `prg32_platform_actor_step(actor, input, speed, jump, gravity, max_fall)`:
  apply left/right movement, jump, gravity, and tile collision.
- `prg32_platform_camera_follow(actor, deadzone_x, deadzone_y)`: follow an actor
  inside the playfield world.

The platform engine intentionally uses integer pixels and small rectangles so
students can inspect every value from C or RISC-V assembly.

Assembly labs can use the `PRG32_PLATFORM_ACTOR_*_OFFSET` macros or treat
`prg32_platform_actor_t` as a 24-byte record:

```text
0:x  4:y  8:vx  12:vy  16:w  18:h  20:state  22:layer
```

## Sprite engine

The sprite layer provides simple bitmap drawing and axis-aligned bounding-box
collision detection.

Useful calls:

- `prg32_sprite_draw_8x8(x, y, bits, fg, bg)`: draw a monochrome sprite.
- `prg32_sprite_draw_16x16(x, y, rgb565)`: draw a 16x16 RGB565 sprite.
- `prg32_sprite_hitbox(...)`: test two axis-aligned rectangles.
- `prg32_sprite_anim_frame(now_ms, frame_count, frame_ms)`: compute a frame.
- `prg32_sprite_draw_frame(...)`: draw one frame from a sprite sheet.

`prg32_sprite_draw_frame` accepts width, height, a pointer to contiguous RGB565
frames, the frame index, and a transparent color. This keeps animated sprites
usable from assembly without requiring a C object.

## Audio

PRG32 has two audio layers.

The legacy teaching helpers still use PWM to drive a passive buzzer:

- `prg32_audio_beep(hz, ms)`
- `prg32_audio_tone(hz, ms, duty)`: PWM tone with explicit duty cycle.
- `prg32_audio_note(midi_note, ms)`: MIDI-like note number to tone.
- `prg32_audio_play_notes(notes, count)`: blocking sequence of notes/rests.
- `prg32_audio_sample_u8(samples, count, rate)`: play unsigned 8-bit samples
  through PWM.

The I2S audio runtime lives in the `prg32_audio` component and targets
MAX98357A DAC/amplifier boards:

- mono mode: one MAX98357A, default, 22050 Hz, 6 voices
- stereo mode: two MAX98357A boards, optional PRG32 Audio Plus, panned voices

Useful calls:

- `prg32_audio_init(config)`: start the I2S mixer runtime.
- `prg32_audio_get_mode()`: return mono or stereo.
- `prg32_audio_register_sample(...)`: register unsigned 8-bit PCM.
- `prg32_audio_play_sample(sample_id, volume, pitch)`: trigger a sample.
- `prg32_audio_play_sample_pan(sample_id, volume, pitch, pan)`: trigger with pan.
- `prg32_audio_note_on(channel, instrument, note, volume)`: start a pitched note.
- `prg32_audio_play_track(track_id)`: start tracker event playback.

Pitch `1024` means natural sample speed. Volumes use `0..255`. Pan uses
`-64..+63`; mono mode accepts pan calls but outputs mono.

See `docs/audio.md` for wiring, examples, and the cartridge AUDIO block format.

The setup audio menu auto-detects the active output path:

- none
- PWM buzzer
- mono I2S
- stereo I2S

It lets trainers set the test volume, play a short tune, and toggle the
onboard RGB LED as a spectrum-style VU meter when the LED GPIO is available.

## Onboard RGB LED

PRG32 exposes a small addressable RGB LED API:

- `prg32_rgb_led_init(gpio)`: initialize the board LED on a free GPIO.
- `prg32_rgb_led_available()`: return whether the LED is ready.
- `prg32_rgb_led_set(red, green, blue)`: set 8-bit RGB intensity.
- `prg32_rgb_led_off()`: turn the LED off.
- `prg32_rgb_led_vu(level)`: map a 0-255 level to a blue/green/yellow/red
  spectrum color.
- `prg32_audio_led_vu_enable(enabled)`: let the audio test and PWM helpers
  drive the LED as a VU meter.

The reference ILI9341 wiring uses GPIO8 for LCD D/C. Many ESP32-C6 development
boards also use GPIO8 for the onboard RGB LED, so `PRG32_PIN_RGB_LED` defaults
to `-1` in `main/prg32_config.h`. Set it only when the LED pin is free on the
chosen board wiring.
