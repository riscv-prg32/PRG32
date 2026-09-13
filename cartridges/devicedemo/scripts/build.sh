#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32_repo="${PRG32_REPO:-"$repo_dir/../.."}"
architecture="${PRG32_ARCHITECTURE:-esp32c6}"
if [[ ! -f "$prg32_repo/prg32/__main__.py" ]]; then echo "error: set PRG32_REPO to a PRG32 checkout" >&2; exit 2; fi
mkdir -p "$repo_dir/dist"
python3 "$prg32_repo/tools/prg32audio_pack.py" "$repo_dir/audio.json" --out "$repo_dir/dist/devicedemo-audio.block"
(cd "$prg32_repo" && python3 -m prg32 cartridge build "$repo_dir/src/devicedemo.c" --entry-prefix devicedemo --name devicedemo --architecture "$architecture" --portable --audio-block "$repo_dir/dist/devicedemo-audio.block" --out "$repo_dir/dist/devicedemo-$architecture.raw.prg32")
(cd "$prg32_repo" && python3 -m prg32 store attach-metadata "$repo_dir/dist/devicedemo-$architecture.raw.prg32" --out "$repo_dir/dist/devicedemo-$architecture.prg32" --metadata "$repo_dir/metadata/metadata.json" --icon "$repo_dir/assets/icon.png" --screenshot "$repo_dir/assets/screenshot.png" --colophon "$repo_dir/metadata/colophon.json" --architecture "$architecture")
echo "$repo_dir/dist/devicedemo-$architecture.prg32"
