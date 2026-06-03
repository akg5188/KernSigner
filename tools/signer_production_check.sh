#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build_wave_43_fresh}"
SDKCONFIG="${1:-${SDKCONFIG:-}}"

if [[ -z "$SDKCONFIG" ]]; then
  if [[ -s "$ROOT_DIR/$BUILD_DIR/sdkconfig" ]]; then
    SDKCONFIG="$ROOT_DIR/$BUILD_DIR/sdkconfig"
  else
    SDKCONFIG="$ROOT_DIR/sdkconfig"
  fi
fi

if [[ -d "$SDKCONFIG" ]]; then
  SDKCONFIG="$SDKCONFIG/sdkconfig"
fi

failures=0

log() {
  printf '[signer-production-check] %s\n' "$*"
}

require_set() {
  local key="$1"
  if grep -Eq "^${key}=y$" "$SDKCONFIG"; then
    log "PASS: $key=y"
    return
  fi
  log "FAIL: $key must be y"
  failures=1
}

require_any_set() {
  local key
  for key in "$@"; do
    if grep -Eq "^${key}=y$" "$SDKCONFIG"; then
      log "PASS: $key=y"
      return
    fi
  done
  log "FAIL: one of [$*] must be y"
  failures=1
}

require_clean_worktree() {
  if ! command -v git >/dev/null 2>&1; then
    log "WARN: git not found; cannot verify release worktree cleanliness"
    return
  fi

  if ! git -C "$ROOT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    log "WARN: not a git worktree; cannot verify release provenance"
    return
  fi

  if [[ -n "$(git -C "$ROOT_DIR" status --short 2>/dev/null)" ]]; then
    log "FAIL: production release requires a clean, committed worktree"
    failures=1
    return
  fi

  log "PASS: git worktree clean"
}

require_not_set() {
  local key="$1"
  if grep -Eq "^${key}=y$" "$SDKCONFIG"; then
    log "FAIL: $key must be disabled"
    failures=1
    return
  fi
  log "PASS: $key disabled"
}

require_comment_not_set() {
  local key="$1"
  if grep -Eq "^# ${key} is not set$" "$SDKCONFIG"; then
    log "PASS: $key is explicitly disabled"
    return
  fi
  if grep -Eq "^${key}=y$" "$SDKCONFIG"; then
    log "FAIL: $key must be disabled"
    failures=1
    return
  fi
  log "WARN: $key not found; treating as disabled"
}

if [[ ! -s "$SDKCONFIG" ]]; then
  log "FAIL: sdkconfig not found: $SDKCONFIG"
  exit 1
fi

log "checking sdkconfig: $SDKCONFIG"

if [[ -s "$ROOT_DIR/sdkconfig.defaults.high_value" ]]; then
  log "PASS: high-value sdkconfig overlay present"
else
  log "FAIL: sdkconfig.defaults.high_value is missing"
  failures=1
fi

if [[ -s "$ROOT_DIR/docs/HIGH_VALUE_SECURITY_TARGET.zh-CN.md" ]]; then
  log "PASS: high-value security target doc present"
else
  log "FAIL: docs/HIGH_VALUE_SECURITY_TARGET.zh-CN.md is missing"
  failures=1
fi

require_set CONFIG_SECURE_BOOT
require_any_set CONFIG_SECURE_FLASH_ENC_ENABLED CONFIG_FLASH_ENCRYPTION_ENABLED
require_set CONFIG_NVS_ENCRYPTION
require_set CONFIG_KSIG_PRODUCTION_REQUIRE_PIN_HMAC

require_comment_not_set CONFIG_BT_ENABLED
require_comment_not_set CONFIG_ESP_HOST_WIFI_ENABLED
require_not_set CONFIG_ESP_WIFI_ENABLED
require_comment_not_set CONFIG_ETH_ENABLED
require_comment_not_set CONFIG_LWIP_ENABLE

require_comment_not_set CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
require_comment_not_set CONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME
require_comment_not_set CONFIG_ESP_SYSTEM_PANIC_GDBSTUB
require_set CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT

require_set CONFIG_ESP_CONSOLE_NONE
require_set CONFIG_ESP_CONSOLE_SECONDARY_NONE
require_comment_not_set CONFIG_ESP_CONSOLE_UART_DEFAULT
require_comment_not_set CONFIG_ESP_CONSOLE_UART_CUSTOM
require_comment_not_set CONFIG_ESP_CONSOLE_UART
require_comment_not_set CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
require_comment_not_set CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
require_comment_not_set CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG

require_set CONFIG_ESP_TASK_WDT_EN
require_set CONFIG_ESP_TASK_WDT_INIT
require_set CONFIG_ESP_TASK_WDT_PANIC

require_clean_worktree

if (( failures != 0 )); then
  cat >&2 <<'EOF'

Production gate failed.

This firmware may still be valid for development and real-device acceptance,
but it must not be released as a commercial real-funds wallet until the failed
security settings are enabled, debug/console paths are disabled, the release
tree is clean, and the irreversible eFuse provisioning workflow has been
reviewed and executed intentionally.
EOF
  exit 1
fi

log "PASS: production security config gate"
