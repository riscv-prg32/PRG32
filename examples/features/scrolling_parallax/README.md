# Scrolling And Parallax Demo

This demo uses two playfield layers:

- layer 0: a star background with slow parallax
- layer 1: foreground terrain with full-speed camera motion

It demonstrates:

- `prg32_tile_define`
- `prg32_playfield_clear`
- `prg32_playfield_put`
- `prg32_playfield_parallax`
- `prg32_playfield_camera`
- `prg32_playfield_draw_dual`

Use entry prefix `scrolling_parallax` for the assembly demo and
`scrolling_parallax_c` for the C demo.
