#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32_repo="${PRG32_REPO:-"$project_dir/../.."}"
architecture="${1:-${PRG32_ARCHITECTURE:-esp32c6}}"
python_bin="${PYTHON:-python3}"
export PYTHONPATH="$prg32_repo${PYTHONPATH:+:$PYTHONPATH}"

if [[ ! -f "$prg32_repo/tools/prg32audio_pack.py" ]]; then
  echo "error: set PRG32_REPO to a development-c6 checkout" >&2
  exit 2
fi

mkdir -p "$project_dir/build" "$project_dir/dist"
"$python_bin" "$project_dir/scripts/generate_audio.py" --out "$project_dir/assets/audio.json"
"$python_bin" "$prg32_repo/tools/prg32audio_pack.py" \
  "$project_dir/assets/audio.json" --out "$project_dir/build/bach-stereo.block"

"$python_bin" -m prg32 cartridge build \
  "$project_dir/src/bach_stereo.c" \
  --portable \
  --entry-prefix bach_stereo \
  --name bach-stereo-showcase \
  --audio-block "$project_dir/build/bach-stereo.block" \
  --out "$project_dir/build/bach-stereo-$architecture.raw.prg32"

attach=("$python_bin" -m prg32 store attach-metadata
  "$project_dir/build/bach-stereo-$architecture.raw.prg32"
  --out "$project_dir/dist/bach-stereo-showcase-$architecture.prg32"
  --metadata "$project_dir/metadata/metadata.json"
  --colophon "$project_dir/metadata/colophon.json"
  --architecture "$architecture")

[[ -f "$project_dir/assets/icon.png" ]] && attach+=(--icon "$project_dir/assets/icon.png")
[[ -f "$project_dir/assets/screenshot.png" ]] && attach+=(--screenshot "$project_dir/assets/screenshot.png")
"${attach[@]}"

echo "$project_dir/dist/bach-stereo-showcase-$architecture.prg32"
