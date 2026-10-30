# Key Gameplay Check

A small deterministic game, and the script that plays it with
[key commands](../../docs/software/usb_protocol.md#key-messages) and checks every
result. It answers a question the protocol tests cannot: does a game behave
correctly when a computer presses the buttons?

The cartridge has no randomness and draws its full state as a strip of white and
black cells at the top right of the screen. `play.py` reads that strip from a
screenshot after each step, so it checks what the game computed and drew.

It checks 29 results, including: each press is exactly one move, walls block,
coins score, hazards cost a life, winning and losing, restarting, a long hold is
one move and counts every frame, a held button charges a shot, A+B arrives in one
frame, pause ignores moves, and 1 ms presses are not lost.

## Run it

Build the cartridge for the target, then play it. Uploading overwrites `--slot`,
`cart7` by default, so save that slot first if it matters.

```bash
python3 -m prg32 cartridge build tests/key_gameplay/game.c --portable \
  --entry-prefix keyplay --name keyplay --architecture esp32c6 --out build/keyplay.prg32
```

```bash
python3 tests/key_gameplay/play.py --port /dev/cu.usbmodem101 \
  --url http://192.168.4.1 --cartridge build/keyplay.prg32
```

The computer must reach the board's HTTP address for its screenshot. On QEMU,
build with `--architecture qemu`, start the emulator with
`-monitor tcp:127.0.0.1:5556,server=on,wait=off`, and replace `--url` with
`--qemu-monitor 127.0.0.1:5556 --qemu-elf build-qemu/PRG32.elf`. Use a firmware
from `python3 -m prg32 qemu build`: the build left by `python3 -m prg32 test`
has no cartridge slots.

It is not part of `python3 -m prg32 test`, because it needs a running target.
