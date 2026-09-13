#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32_repo="${PRG32_REPO:-"$repo_dir/../../.."}"
stage="$repo_dir/dist/store-bundle"; rm -rf "$stage"; mkdir -p "$stage"
cp "$repo_dir/metadata/manifest.json" "$stage/manifest.json"
cp "$repo_dir/assets/icon.png" "$stage/icon.png"; cp "$repo_dir/assets/screenshot.png" "$stage/screenshot.png"
cp "$repo_dir/dist/devicedemo-esp32c6.prg32" "$stage/devicedemo-esp32c6.prg32"; cp "$repo_dir/dist/devicedemo-qemu.prg32" "$stage/devicedemo-qemu.prg32"
(cd "$prg32_repo" && python3 -m prg32 store pack-bundle --manifest "$stage/manifest.json" --out "$repo_dir/dist/devicedemo-store-bundle.zip")
echo "$repo_dir/dist/devicedemo-store-bundle.zip"
