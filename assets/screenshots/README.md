# Screenshots
Add screenshots in `assets/screenshots` with these names:
- `prg32_menu_<menu_name>` - Screenshot of a section of the menu of prg32
- `prg32_game_<game>.png` - Screenshot of a game running on prg32
- `featured/<game>.png` - Store screenshot (320x200) of a featured cartridge
  from its own repository, shown in the main README's Featured Games section

`prg32_menu_about.png` is the firmware colophon. Recapture it from QEMU when the
contributor list in `about_menu()` changes.

Capture tips:
- Keep terminal width around 100 columns if running on QEMU.
- Use PNG format and avoid heavy compression artifacts.
