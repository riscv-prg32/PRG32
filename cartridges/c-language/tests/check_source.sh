#!/usr/bin/env bash
# Static checks for the PRG32 C cartridge (no toolchain needed).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 -m json.tool "$root/metadata/metadata.json" >/dev/null
python3 -m json.tool "$root/metadata/colophon.json" >/dev/null
src="$root/src/c_language.c"
for fn in prg32_btkbd_read_key prg32_btkbd_set_mapping prg32_btkbd_state prg32_btkbd_key_down; do
  grep -q "$fn" "$src" || { echo "missing $fn" >&2; exit 1; }
done
# Portable cartridges are not relocated: no initialized pointer tables.
if grep -Eq 'static const char \*[a-z_]+\[\]' "$src"; then
  echo "initialized pointer table found in $src" >&2
  exit 1
fi
if grep -Eq '#include <(stdio|stdlib|string)\.h>' "$src"; then
  echo "cartridges must not use the C library" >&2
  exit 1
fi
echo "PRG32 C source checks passed"
