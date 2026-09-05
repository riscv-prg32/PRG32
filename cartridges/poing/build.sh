#!/usr/bin/env sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=${PRG32_REPO:-${1:-"$HERE/../.."}}
ARCH=${2:-${PRG32_ARCHITECTURE:-esp32c6}}
mkdir -p "$HERE/dist"
cd "$ROOT"
python3 -m prg32 cartridge build "$HERE/poing.c" \
  --portable --entry-prefix poing --name "Poing" \
  --out "$HERE/dist/poing-$ARCH-core.prg32"
python3 -m prg32 store attach-metadata "$HERE/dist/poing-$ARCH-core.prg32" \
  --metadata "$HERE/metadata.json" --icon "$HERE/assets/icon.png" \
  --screenshot "$HERE/assets/screenshot.png" \
  --colophon "$HERE/colophon.json" --architecture "$ARCH" \
  --out "$HERE/dist/poing-$ARCH.prg32"
echo "Built $HERE/dist/poing-$ARCH.prg32"
