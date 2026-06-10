# PRG32 ABI

PRG32 portable cartridges call framework functions through a stable, versioned
ABI table supplied by the resident firmware. The table is generated from
`tools/prg32_abi.json`; generated files contain the function indices, ABI hash,
and firmware table population code.

Legacy cartridges can still use firmware-specific absolute imports resolved
from `/api/runtime` or from a firmware ELF, but that mode is tied to one
firmware image. New cartridges should be built with `--portable`.

## Register Convention

PRG32 follows the standard RISC-V calling convention:

| Register | Purpose |
|---|---|
| `a0`-`a7` | arguments and return values |
| `ra` | return address |
| `sp` | 16-byte aligned stack |
| `t0`-`t6` | caller-saved temporaries |
| `s0`-`s11` | callee-saved values |

Assembly examples save `ra` around C calls and keep stack alignment visible.
For portable cartridges, `a0` contains a pointer to `prg32_abi_table_t` when
the runtime enters `init`, `update`, or `draw`. The cartridge-side stubs emitted
by `tools/prg32_game.py --portable` store that pointer in `__prg32_abi` and keep
the familiar `call prg32_gfx_clear` style available to examples.

## Stable ABI Table

The firmware exposes one `prg32_abi_table` with magic `PABI`, ABI major/minor,
the generated ABI hash, feature bits, and an indexed function pointer array.
Cartridges declare the ABI hash and required features in the v2 cartridge
header.

Compatibility rules:

- same ABI major and matching hash: accepted
- missing required feature bits: rejected
- newer incompatible major: rejected
- legacy absolute imports: supported only for firmware-specific workflows

Feature bits currently cover audio, Wi-Fi, multiplayer, metrics, audio-plus,
keyboard, tilemap, platformer, and sprites.

## Cartridge Package ABI

The executable cartridge ABI remains `PRG2` major `1`, minor `0`. Header v2
extends the original header via `header_size` with `abi_hash`,
`required_features`, `optional_features`, relocation placeholders, and
`import_model`. `import_model=abi-table` marks a portable cartridge;
`import_model=legacy-absolute` marks the older firmware-specific path.

Store-ready cartridges append a backward-compatible `PRG32META` trailer after
the payload. The trailer gives host tools and setup-mode clients standard
blocks for `META`, `ICON`, `SCRN`, `SIGN`, and `COLO`.

Metadata JSON uses `prg32-metadata-1.0`; colophon JSON uses
`prg32-colophon-1.0`. The game colophon is shown after the cartridge is
activated, before the player starts a new play.

## Audio ABI Calls

The audio ABI is the C API exposed to cartridges:

| Symbol | Purpose | Return |
|---|---|---|
| `prg32_audio_init` | initialize mono/stereo runtime | `bool` |
| `prg32_audio_shutdown` | stop audio runtime | none |
| `prg32_audio_get_mode` | return `PRG32_AUDIO_MODE_MONO` or `STEREO` | mode |
| `prg32_audio_play_sample` | play sample centered | channel or negative |
| `prg32_audio_play_sample_pan` | play sample with pan | channel or negative |
| `prg32_audio_stop_channel` | stop one voice | none |
| `prg32_audio_stop_all` | stop all voices | none |
| `prg32_audio_note_on` | start instrument note | none |
| `prg32_audio_note_on_pan` | start instrument note with pan | none |
| `prg32_audio_note_off` | stop note channel | none |
| `prg32_audio_play_track` | start tracker stream | none |
| `prg32_audio_stop_track` | stop tracker stream | none |
| `prg32_audio_set_tempo` | set tracker BPM | none |
| `prg32_audio_set_master_volume` | set global volume | none |
| `prg32_audio_set_channel_volume` | set one voice volume | none |
| `prg32_audio_set_channel_pan` | set one voice pan | none |
| `prg32_audio_led_vu_enable` | allow audio helpers to drive the RGB LED VU meter | none |
| `prg32_audio_led_vu_enabled` | read the RGB LED VU meter flag | `int` |
| `prg32_audio_led_vu_level` | update the RGB LED VU level if enabled | none |

Pan uses signed values:

```text
-64 full left, 0 center, +63 full right
```

Mono builds accept pan calls but mix to one output. Stereo-only programs should
check `prg32_audio_get_mode()` before making a wiring assumption.

## RGB LED ABI Calls

The onboard RGB LED API is optional because many classroom display harnesses use
the same GPIO as the board LED. Check availability before depending on it.

| Symbol | Purpose | Return |
|---|---|---|
| `prg32_rgb_led_init` | initialize an addressable RGB LED on a GPIO | `0` or negative |
| `prg32_rgb_led_available` | report whether the LED is ready | `int` |
| `prg32_rgb_led_set` | set red, green, blue intensity | none |
| `prg32_rgb_led_off` | turn the LED off | none |
| `prg32_rgb_led_vu` | map a 0-255 level to spectrum color | none |

## Error Values

Audio calls that return `int` use a non-negative channel number for success and
a negative value for failure. Common failure causes:

- audio runtime was not initialized
- sample id is missing
- channel id is outside the configured voice table
- AUDIO block is invalid

## Assembly Example

```asm
    li a0, 0          /* sample id */
    li a1, 255        /* volume */
    li a2, 1024       /* natural pitch */
    call prg32_audio_play_sample
```

For stereo pan:

```asm
    li a0, 0
    li a1, 255
    li a2, 1024
    li a3, -64        /* left */
    call prg32_audio_play_sample_pan
```

## Splash ABI Calls

Splash helpers are exported for cartridges and examples:

| Symbol | Purpose |
|---|---|
| `prg32_splash_draw_game` | draw a 320x200 game title screen without delaying |
| `prg32_splash_show_game` | draw a 320x200 game title screen, present, and wait |
| `prg32_splash_draw` | draw a full 320x240 framework splash/title screen without delaying |
| `prg32_splash_show` | draw a full 320x240 framework splash, present, and wait |
| `prg32_splash_show_default` | show the built-in PRG32 startup splash |
| `prg32_gfx_lock` | enter the recursive graphics critical section |
| `prg32_gfx_unlock` | leave the recursive graphics critical section |
| `prg32_gfx_set_fullscreen` | use 320x240 coordinates for framework/title screens |
| `prg32_gfx_fullscreen_enabled` | return whether full-screen drawing is active |
| `prg32_gfx_set_band_color` | set a custom top/bottom band color for games |
| `prg32_gfx_use_background_bands` | make game bands follow `prg32_gfx_clear` again |
| `prg32_gfx_snapshot_row_rgb565` | copy a physical framebuffer row as RGB565 |
| `prg32_band_set_mode` | choose what status data a band renders |
| `prg32_band_mode` | read the current mode for a band |
| `prg32_band_set_text` | set custom band text |
| `prg32_band_set_game_info` | set game status text |
| `prg32_band_log` | set debug/status log text |
| `prg32_band_set_colors` | set foreground/background colors for one band |
| `prg32_band_use_default_colors` | make a band use the game background color again |

`prg32_splash_show_game` and `prg32_splash_show` arguments:

| Register | Value |
|---|---|
| `a0` | title C string |
| `a1` | subtitle C string |
| `a2` | duration in milliseconds |
| `a3` | RGB565 background color |
| `a4` | RGB565 foreground color |
| `a5` | RGB565 accent color |

Example:

```asm
    la a0, game_title
    la a1, game_subtitle
    li a2, 900
    li a3, 0x0000
    li a4, 0xffff
    li a5, 0x07ff
    call prg32_splash_show_game
```

Band identifiers:

| Constant | Value |
|---|---:|
| `PRG32_BAND_TOP` | 0 |
| `PRG32_BAND_BOTTOM` | 1 |

Band modes:

| Constant | Meaning |
|---|---|
| `PRG32_BAND_MODE_NONE` | hide band text |
| `PRG32_BAND_MODE_FPS` | show measured frame rate |
| `PRG32_BAND_MODE_WIFI` | show current SSID and IP |
| `PRG32_BAND_MODE_GAME` | show cartridge/game info |
| `PRG32_BAND_MODE_DEBUG` | show the last debug message |
| `PRG32_BAND_MODE_CUSTOM` | show text set with `prg32_band_set_text` |

## Input And Setup ABI Calls

Setup screens and cartridge programs use the same button bitmasks:

| Symbol | Purpose |
|---|---|
| `prg32_input_read` | read the local player input bitmask |
| `prg32_input_read_player` | read player 1 normalized to low bits; player 2 returns `0` |
| `prg32_input_read_menu` | read local joystick input for setup/menu navigation |
| `prg32_input_wait_released` | wait until selected menu bits are released |
| `prg32_wifi_current_mode` | return the active Wi-Fi mode enum |
| `prg32_wifi_current_ip` | return the current IP display string |
| `prg32_wifi_current_ssid` | return the current AP or infrastructure SSID |
| `prg32_multiplayer_init` | initialize the multiplayer service |
| `prg32_multiplayer_available` | return whether cartridge multiplayer can be used |
| `prg32_multiplayer_join` | join peers with the same cartridge signature |
| `prg32_multiplayer_leave` | leave the current multiplayer room |
| `prg32_multiplayer_tick` | service periodic multiplayer sends and peer expiry |
| `prg32_multiplayer_set_local_state` | publish local player position and sprite status |
| `prg32_multiplayer_set_input` | publish local player input |
| `prg32_multiplayer_get_peer_count` | return visible peer count |
| `prg32_multiplayer_get_peer` | copy one peer snapshot |
| `prg32_cart_default_slot` | return the saved default cartridge slot, or `-1` |
| `prg32_cart_set_default_slot` | save a default cartridge slot, or clear with `-1` |
| `prg32_cart_select_default` | load the saved default cartridge |
| `prg32_performance_test_run` | run the unattended multi-screen setup benchmark |
| `prg32_performance_has_results` | return nonzero when onboard benchmark results are available |
| `prg32_performance_summary` | copy the latest benchmark summary into a caller-provided struct |

`PRG32_BTN_SELECT` is the classroom-facing name for the select button.
`PRG32_BTN_START` remains an alias for existing code.
