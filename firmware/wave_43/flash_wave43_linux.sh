#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

python3 -m esptool --chip esp32p4 -p "$PORT" -b 115200 \
  --before default_reset --after hard_reset write_flash 0x0 \
  "$DIR/kernsigner-wave43-0.0.7-rc1-untested-full.bin"
