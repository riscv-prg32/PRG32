#!/usr/bin/env bash
# Build the PRG32 C cartridge for one architecture (PRG32_ARCHITECTURE=esp32c6|qemu).
set -euo pipefail
cart_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="${PRG32_REPO:-"$cart_dir/../.."}"
arch="${PRG32_ARCHITECTURE:-esp32c6}"
mkdir -p "$cart_dir/dist"
(cd "$repo_dir" && python3 -m prg32 cartridge build "$cart_dir/src/c_language.c" --portable --entry-prefix c_language --name c-language --architecture "$arch" --required-feature btkeyboard --out "$cart_dir/dist/c-language-$arch.raw.prg32")
(cd "$repo_dir" && python3 -m prg32 store attach-metadata "$cart_dir/dist/c-language-$arch.raw.prg32" --out "$cart_dir/dist/c-language-$arch.prg32" --metadata "$cart_dir/metadata/metadata.json" --icon "$cart_dir/assets/icon.png" --colophon "$cart_dir/metadata/colophon.json" --architecture "$arch")
echo "$cart_dir/dist/c-language-$arch.prg32"
