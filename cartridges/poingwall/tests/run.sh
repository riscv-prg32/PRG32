#!/usr/bin/env sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cc -std=c17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$HERE/include" "$HERE/../poingwall.c" "$HERE/host_test.c" \
  -o "$HERE/poingwall-host-test"
"$HERE/poingwall-host-test"
rm -f "$HERE/poingwall-host-test"
python3 -m json.tool "$HERE/../metadata.json" >/dev/null
python3 -m json.tool "$HERE/../colophon.json" >/dev/null
echo "metadata validation: PASS"
