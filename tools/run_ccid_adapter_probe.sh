#!/usr/bin/env bash
set -euo pipefail

SIGNER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE_DIR="${CCID_PROBE_DIR:-/home/ak/123/seedsigner/esp32_p4_ccid_probe}"
LOG_DIR="${CCID_LOG_DIR:-${SIGNER_DIR}/docs/logs}"
MODE="${1:-run}"

if [[ ! -d "$PROBE_DIR" ]]; then
  echo "CCID probe project not found: $PROBE_DIR" >&2
  exit 1
fi

mkdir -p "$LOG_DIR"

case "$MODE" in
  build)
    shift || true
    BUILD_ONLY=1 LOG_DIR="$LOG_DIR" JOBS="${JOBS:-2}" "$PROBE_DIR/flash_monitor.sh" build "$@"
    ;;
  run|flash|monitor|read)
    shift || true
    LOG_DIR="$LOG_DIR" JOBS="${JOBS:-2}" MONITOR_SECONDS="${MONITOR_SECONDS:-90}" "$PROBE_DIR/flash_monitor.sh" "$MODE" "$@"
    ;;
  *)
    LOG_DIR="$LOG_DIR" JOBS="${JOBS:-2}" MONITOR_SECONDS="${MONITOR_SECONDS:-90}" "$PROBE_DIR/flash_monitor.sh" run "$@"
    ;;
esac

latest_log="$(find "$LOG_DIR" -maxdepth 1 -type f -name 'ccid_probe_*.log' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)"
if [[ -n "$latest_log" ]]; then
  echo "Latest CCID probe log: $latest_log"
fi
