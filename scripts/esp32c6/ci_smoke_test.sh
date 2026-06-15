#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

export PYTHONPYCACHEPREFIX="${PYTHONPYCACHEPREFIX:-/tmp/prg32-pycache}"

echo "[CI] Checking whitespace"
git diff --check

echo "[CI] Compiling Python tools"
python3 -m py_compile \
  python3 -m prg32 \
  tools/prg32_metrics_paper.py

echo "[CI] Running unit tests"
python3 -m unittest discover -s tests

echo "[CI] Running host-only cartridge doctor"
python3 -m prg32 doctor --host-only

echo "[CI] Host smoke test passed"
