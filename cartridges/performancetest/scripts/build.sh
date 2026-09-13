#!/usr/bin/env bash
set -euo pipefail
cart_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="${PRG32_REPO:-"$cart_dir/../.."}"
arch="${PRG32_ARCHITECTURE:-esp32c6}"
mkdir -p "$cart_dir/dist"
(cd "$repo_dir" && python3 -m prg32 cartridge build "$cart_dir/src/performancetest.c" --portable --entry-prefix performancetest --name performancetest --architecture "$arch" --required-feature metrics --required-feature sprites --out "$cart_dir/dist/performancetest-$arch.raw.prg32")
(cd "$repo_dir" && python3 -m prg32 store attach-metadata "$cart_dir/dist/performancetest-$arch.raw.prg32" --out "$cart_dir/dist/performancetest-$arch.prg32" --metadata "$cart_dir/metadata/metadata.json" --icon "$repo_dir/docs/measurement/images/performance_rgb565_vs_indexed.png" --screenshot "$repo_dir/docs/measurement/images/performance_rgb565_vs_indexed.png" --colophon "$cart_dir/metadata/colophon.json" --architecture "$arch")
echo "$cart_dir/dist/performancetest-$arch.prg32"
