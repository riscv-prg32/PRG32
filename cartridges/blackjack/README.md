# PRG32 Blackjack — Casino Edition

A complete Blackjack cartridge targeting the PRG32 `development-c6` firmware line. It uses the branch's indexed-color sprite API and SID-like procedural audio/tracker support.

![Blackjack gameplay screenshot](screenshot.png)

[Download the 30-second MP4 captured from actual QEMU gameplay and UART audio](preview.mp4).

## Game modes

- **Solo:** casino blackjack with persistent PRG32 high-score submission based on bankroll.
- **Hot-seat 2–4 players:** all players share one shoe and dealer, with independent bankrolls, bets, split hands, insurance, doubling and settlement.
- **Network Table:** joins the PRG32 multiplayer service and publishes live bankroll/hand-state presence to peers while each device plays its local table. The current compact multiplayer ABI does not synchronize an authoritative shared shoe, so the cartridge does not pretend that network peers are receiving the same cards.

## Rules implemented

Six-deck shoe by default; optional single deck; configurable S17/H17; blackjack 3:2 or 6:5; dealer peek; insurance at half the original wager paying 2:1; late surrender; double down; double after split; splitting and resplitting up to four hands; split aces receive one card and stand; split-hand 21 is not treated as a natural blackjack; automatic reshuffle after 75% penetration; bust/push/dealer-bust settlement; bankroll reset if all players are below the table minimum.

Virtual credits only: there is no real-money wagering or purchase path.

## Controls

A = hit/confirm, B = stand/back, Up = double or increase wager, Down = split or decrease wager, Left = surrender or smaller wager adjustment, Right = larger wager adjustment, Select = scores/exit betting.

## Graphics and sound

The table is drawn directly in the 320×200 PRG32 viewport. Cards use high-contrast suit/rank rendering, compact settlement labels that remain readable with four split hands, and a compact 4-bpp indexed-color card-back sprite. `audio.json` defines procedural synth instruments for soundtrack voices and card/chip/win/lose effects; no PCM soundtrack is needed, preserving cartridge space.

## Build

Place this directory at `cartridges/blackjack` in a checkout of `riscv-prg32/PRG32` on branch `development-c6`, then run:

```sh
./cartridges/blackjack/build.sh
```

Expected products:

```text
cartridges/blackjack/dist/store/blackjack-esp32c6.prg32
cartridges/blackjack/dist/store/blackjack-qemu.prg32
cartridges/blackjack/dist/blackjack-1.0.0-store.zip
```

Upload hardware cartridge using the branch CLI, for example:

```sh
python3 -m prg32 esp32c6 upload cartridges/blackjack/dist/store/blackjack-esp32c6.prg32 --url http://192.168.4.1
```

For the Cartridge Store, publish the generated bundle with the configured store URL/token using `python3 -m prg32 publish-bundle`.

## Host-side validation

```sh
clang -std=c11 -Wall -Wextra -Werror tests/test_rules.c blackjack_rules.c -o build/test_rules
./build/test_rules
```

`tests/stub/prg32.h` exists only for host syntax checking and is never packed into the cartridge.

## CI/CD

`.github/workflows/ci.yml` runs the rules test, verifies `SHA256SUMS`, builds
both architecture variants, inspects the packaged metadata, tests the store
ZIP, validates the screenshot and audiovisual preview, and uploads
`blackjack-cartridge-package` for 14 days. The artifact is a
build product; publishing to a Cartridge Store still requires explicit
credentials and a separate release decision.
