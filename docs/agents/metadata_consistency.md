# Metadata Consistency Rules

When changing authorship or academic attribution, keep these files in sync:

- `README.md`
- `CONTRIBUTORS.md`
- `CONTRIBUTING.md`
- `CITATION.cff`
- the firmware colophon: `about_menu()` in `components/prg32/prg32_system.c`
  (names start at x=8, roles at x=152; keep every line within 320 pixels) and
  its screenshot `assets/screenshots/prg32_menu_about.png`

Do not silently remove existing contributor attribution unless explicitly requested.
