#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$project_dir/scripts/generate_audio.py" --out "$project_dir/assets/audio.json" --check
python3 "$project_dir/tests/test_score.py"
cc -std=c17 -Wall -Wextra -Werror -fsyntax-only \
  -I"$project_dir/tests/include" "$project_dir/src/bach_stereo.c"
echo "All Bach Stereo Showcase checks passed."
