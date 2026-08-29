# VS Code Student Setup

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
- `python3 -m prg32 cartridge build ...`
- `python3 -m prg32 upload ...`
- `python3 -m prg32 upload-qemu ...`

Do not hard-code one instructor machine path. Use workspace-relative paths and
standard ESP-IDF configuration variables where possible.
