#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="$HERE/build"
DIST="$HERE/dist"
STORE="$DIST/store"
mkdir -p "$BUILD" "$STORE"
cd "$ROOT"
python3 tools/prg32audio_pack.py "$HERE/audio.json" --out "$BUILD/blackjack-audio.block"
python3 -m prg32 cartridge build "$HERE/game.c" \
  --portable --entry-prefix blackjack --name blackjack --multiplayer \
  --audio-block "$BUILD/blackjack-audio.block" --out "$BUILD/blackjack-base.prg32"
for arch in esp32c6 qemu; do
  python3 -m prg32 store attach-metadata "$BUILD/blackjack-base.prg32" \
    --metadata "$HERE/metadata.json" --icon "$HERE/icon.png" \
    --screenshot "$HERE/screenshot.png" --colophon "$HERE/colophon.json" \
    --architecture "$arch" --out "$STORE/blackjack-$arch.prg32"
done
cp "$HERE/manifest.json" "$HERE/icon.png" "$HERE/screenshot.png" "$HERE/colophon.json" "$STORE/"
python3 -m prg32 cartridge summary "$STORE/blackjack-esp32c6.prg32"
python3 -m prg32 store inspect-metadata "$STORE/blackjack-esp32c6.prg32"
python3 -m prg32 store pack-bundle --manifest "$STORE/manifest.json" --out "$DIST/blackjack-1.0.0-store.zip"
echo "Built: $STORE/blackjack-esp32c6.prg32"
echo "Built: $STORE/blackjack-qemu.prg32"
echo "Built: $DIST/blackjack-1.0.0-store.zip"
