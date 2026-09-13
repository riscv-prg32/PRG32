#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 -m json.tool "$root/metadata/metadata.json" >/dev/null
python3 -m json.tool "$root/metadata/manifest.json" >/dev/null
python3 -m json.tool "$root/metadata/colophon.json" >/dev/null
python3 -m json.tool "$root/audio.json" >/dev/null
grep -q 'prg32_sprite_draw_indexed' "$root/src/devicedemo.c"
grep -q 'prg32_sprite_draw_bitplanes' "$root/src/devicedemo.c"
grep -q '"tracks"' "$root/audio.json"
grep -q '"sample_id"' "$root/audio.json"
if grep -Eq 'prg32_(audio_is_ready|audio_register_instrument|audio_register_track|audio_led_vu_enable|memory_get_stats|store_url_get|diag_)' "$root/src/devicedemo.c"; then
  echo "DeviceDemo+ uses a firmware-private helper" >&2
  exit 1
fi
echo "DeviceDemo+ source checks passed"
