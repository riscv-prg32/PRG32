# Audio Pan Test

This source-only cartridge cycles three notes through the left, right, and
center positions. Press A to pause or resume. It reads the configured mono or
stereo audio mode through the public PRG32 API.

After sourcing ESP-IDF, build a portable cartridge from the repository root:

```bash
python3 -m prg32 cartridge build cartridges/audiotest/src/audiotest.c \
  --portable --entry-prefix audiotest --name AudioTest \
  --out build-esp32c6/audiotest.prg32
python3 -m prg32 cartridge summary build-esp32c6/audiotest.prg32
```

The resulting package uses the current ABI table and has no external audio
asset or Store metadata block.
