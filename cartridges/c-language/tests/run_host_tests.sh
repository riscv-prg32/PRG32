#!/usr/bin/env bash
# Compile the C interpreter cartridge for the host and run its test programs.
set -euo pipefail
cart_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="${PRG32_REPO:-"$cart_dir/../.."}"
out="$(mktemp -d)/c_language_host_test"
cc -std=c99 -Wall -Wextra -O1 \
  -I "$repo_dir/components/prg32/include" \
  -I "$repo_dir/components/prg32_audio/include" \
  "$cart_dir/tests/host_test.c" -o "$out"
"$out"
