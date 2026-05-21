#!/bin/bash

# Run all CI checks for a single commit: format, tests, and the supported wave_43 build.
# Invoked from the per-commit CI loop (see .github/workflows/test-each-commit.yml)
# and runnable locally to reproduce what CI does.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || (cd "$SCRIPT_DIR/.." && pwd))"
cd "$REPO_ROOT"

echo "=== Commit under test ==="
git log -1 --oneline

echo "=== format check ==="
"$SCRIPT_DIR/format.sh" --check

echo "=== tests ==="
"$SCRIPT_DIR/test.sh"

echo "=== build wave_43 ==="
# Local dev: load ESP-IDF if idf.py isn't already on PATH. In CI the job
# sources export.sh once before the per-commit loop, so this is a no-op.
command -v idf.py >/dev/null || . "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh"
idf.py \
    -B build_wave_43_fresh \
    -D SDKCONFIG=build_wave_43_fresh/sdkconfig \
    -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
    build
