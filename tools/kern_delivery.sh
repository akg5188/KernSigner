#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_PATH_DEFAULT="/home/ak/esp-idf-v5.5.4"
IDF_PATH="${IDF_PATH:-$IDF_PATH_DEFAULT}"
ESPPORT="${ESPPORT:-/dev/ttyACM0}"
ESPBAUD="${ESPBAUD:-115200}"
BUILD_DIR="${BUILD_DIR:-build_wave_43_fresh}"
SIM_BUILD_DIR="${SIM_BUILD_DIR:-build}"
SIM_BOARD="${SIM_BOARD:-wave_43}"
SIM_WIDTH="${SIM_WIDTH:-480}"
SIM_HEIGHT="${SIM_HEIGHT:-800}"
JOBS="${JOBS:-2}"
ACTION="${1:-all}"
VERIFY_DIR="${2:-}"
EXPECTED_HOME_TITLE="离线签名器"
LAST_SCREENSHOT_DIR=""
LAST_ACCEPTANCE_REPORT=""
LAST_BOOT_LOG=""
KEY_SCREENSHOT_IDS=(
  "home"
  "pi_mnemonic_tools"
  "pi_connect_wallet"
  "custom_derivation"
  "load_mnemonic"
  "new_seedkeeper_create_mnemonic"
  "load_seedkeeper_mnemonic"
  "load_steel_restore"
  "load_punch_grid"
  "load_tinyseed_restore"
  "load_stackbit_restore"
  "backup_export"
  "backup_seed_words"
  "backup_entropy"
  "backup_steel_punch"
  "backup_stackbit"
  "backup_grid"
  "backup_seed_qr"
  "backup_kef"
  "connect_okx"
  "connect_bitget"
  "connect_metamask"
  "connect_rabby"
  "connect_tokenpocket"
  "web3_okx_mnemonic"
  "web3_okx_satochip"
  "connect_address"
  "btc_wallet"
  "btc_mnemonic"
  "btc_satochip"
  "tools_create_qr"
  "tools_qr_capture"
  "tools_file_manager"
  "settings_display"
  "settings_camera"
  "system_overview"
  "about"
)

log() {
  printf '[kern-delivery] %s\n' "$*"
}

source_idf() {
  # shellcheck source=/dev/null
  source "$IDF_PATH/export.sh" >/tmp/kern_idf_export.log
}

bake_fonts() {
  log "bake Chinese font subset"
  (cd "$ROOT_DIR" && tools/bake_krux_cn_fonts.py)
}

build_sim() {
  log "build simulator (${SIM_BOARD} ${SIM_WIDTH}x${SIM_HEIGHT})"
  (cd "$ROOT_DIR/simulator" && cmake -B "$SIM_BUILD_DIR" -S . \
    -DSIM_BOARD="$SIM_BOARD" \
    -DSIM_LCD_H_RES="$SIM_WIDTH" \
    -DSIM_LCD_V_RES="$SIM_HEIGHT")
  (cd "$ROOT_DIR/simulator" && cmake --build "$SIM_BUILD_DIR" -- -j"$JOBS")
}

screenshots() {
  local stamp out
  stamp="$(date +%Y%m%d_%H%M%S)"
  out="$ROOT_DIR/docs/screens/delivery_$stamp"
  mkdir -p "$out"
  log "capture simulator screenshots: $out"
  (cd "$ROOT_DIR" && SDL_VIDEODRIVER=dummy simulator/build/kern_simulator \
    --width "$SIM_WIDTH" --height "$SIM_HEIGHT" --screenshot-dir "$out")
  LAST_SCREENSHOT_DIR="$out"
}

latest_screenshot_dir() {
  find "$ROOT_DIR/docs/screens" -maxdepth 1 -type d -name 'delivery_*' -printf '%T@ %p\n' \
    | sort -nr \
    | awk 'NR == 1 {print $2}'
}

latest_release_dir() {
  awk '
    /^Recommended delivery package:/ {
      getline
      print
      exit
    }
  ' "$ROOT_DIR/_release/LATEST_RELEASE.txt" 2>/dev/null
}

production_sdkconfig_path() {
  if [[ -n "${SDKCONFIG:-}" ]]; then
    printf '%s\n' "$SDKCONFIG"
    return
  fi

  if [[ -s "$ROOT_DIR/$BUILD_DIR/sdkconfig" ]]; then
    printf '%s\n' "$ROOT_DIR/$BUILD_DIR/sdkconfig"
    return
  fi

  printf '%s\n' "$ROOT_DIR/sdkconfig"
}

verify_screenshots() {
  local dir="${1:-}"
  local missing=0
  local id bmp filename title
  local manifest glyph_report smoke_report scroll_report interaction_report report
  local total_pages top_bmp_count bottom_bmp_count bmp_count bad_capture
  local bad_glyphs bad_smoke bad_scroll bad_interaction scroll_pages interaction_checks home_title
  local deprecated_manifest_ids deprecated_interaction_ids

  if [[ -z "$dir" ]]; then
    dir="$(latest_screenshot_dir)"
  fi

  if [[ -z "$dir" || ! -d "$dir" ]]; then
    log "verify failed: screenshot directory not found"
    return 1
  fi

  log "verify key screenshots in: $dir"
  manifest="$dir/manifest.tsv"
  glyph_report="$dir/glyph_check.tsv"
  smoke_report="$dir/smoke_check.tsv"
  scroll_report="$dir/scroll_check.tsv"
  interaction_report="$dir/interaction_check.tsv"
  report="$dir/ACCEPTANCE_REPORT.txt"
  LAST_ACCEPTANCE_REPORT="$report"

  {
    echo "Kern Acceptance Report"
    echo "======================"
    echo
    echo "Time: $(date -Iseconds)"
    echo "Screenshot directory: $dir"
    echo "Target board: wave_43"
    echo
  } >"$report"

  if [[ ! -s "$manifest" ]]; then
    log "missing manifest: $manifest"
    echo "FAIL: missing simulator manifest.tsv" >>"$report"
    return 1
  fi

  if [[ ! -s "$glyph_report" ]]; then
    log "missing glyph report: $glyph_report"
    echo "FAIL: missing glyph_check.tsv" >>"$report"
    return 1
  fi

  if [[ ! -s "$smoke_report" ]]; then
    log "missing smoke report: $smoke_report"
    echo "FAIL: missing smoke_check.tsv" >>"$report"
    return 1
  fi

  if [[ ! -s "$scroll_report" ]]; then
    log "missing scroll report: $scroll_report"
    echo "FAIL: missing scroll_check.tsv" >>"$report"
    return 1
  fi

  if [[ ! -s "$interaction_report" ]]; then
    log "missing interaction report: $interaction_report"
    echo "FAIL: missing interaction_check.tsv" >>"$report"
    return 1
  fi

  total_pages="$(awk 'NR > 1 {n++} END {print n + 0}' "$manifest")"
  bmp_count="$(find "$dir" -maxdepth 1 -type f -name '*.bmp' | wc -l | tr -d ' ')"
  top_bmp_count="$(awk -F '\t' -v dir="$dir" 'NR > 1 {f=dir "/" $4; if (system("[ -s \"" f "\" ]") == 0) n++} END {print n + 0}' "$manifest")"
  bottom_bmp_count="$(find "$dir" -maxdepth 1 -type f -name '*_bottom.bmp' | wc -l | tr -d ' ')"
  bad_capture="$(awk -F '\t' 'NR > 1 && $7 != "ok" {n++} END {print n + 0}' "$manifest")"
  bad_glyphs="$(awk -F '\t' 'NR > 1 {n += $8 + 0} END {print n + 0}' "$manifest")"
  bad_smoke="$(awk -F '\t' 'NR > 1 && $5 != "ok" {n++} END {print n + 0}' "$smoke_report")"
  bad_scroll="$(awk -F '\t' 'NR > 1 && $7 == "failed" {n++} END {print n + 0}' "$scroll_report")"
  bad_interaction="$(awk -F '\t' 'NR > 1 && $6 !~ /^ok/ {n++} END {print n + 0}' "$interaction_report")"
  deprecated_manifest_ids="$(awk -F '\t' '
    NR > 1 &&
      ($2 ~ /(web3_message_sign|web3_typed_data|test_usb_ccid)/ ||
       $3 ~ /(Web3 消息签名|结构化数据签名)/) {
      if (out) out = out ", ";
      out = out $2 "(" $3 ")";
    }
    END {print out}
  ' "$manifest")"
  deprecated_interaction_ids="$(awk -F '\t' '
    NR > 1 &&
      ($1 ~ /(web3_message_sign|web3_typed_data|test_usb_ccid)/ ||
       $2 ~ /(Web3 消息签名|结构化数据签名)/ ||
       $3 ~ /(web3_message_sign|web3_typed_data|test_usb_ccid)/) {
      if (out) out = out ", ";
      out = out $1 "->" $3;
    }
    END {print out}
  ' "$interaction_report")"
  scroll_pages="$(awk -F '\t' 'NR > 1 && $3 == "yes" {n++} END {print n + 0}' "$scroll_report")"
  interaction_checks="$(awk -F '\t' 'NR > 1 {n++} END {print n + 0}' "$interaction_report")"
  home_title="$(awk -F '\t' 'NR > 1 && $2 == "home" {print $3; exit}' "$manifest")"

  {
    echo "Simulator:"
    echo "- Pages in manifest: $total_pages"
    echo "- Top BMP screenshots: $top_bmp_count"
    echo "- Bottom BMP screenshots: $bottom_bmp_count"
    echo "- Total BMP screenshots: $bmp_count"
    echo "- Capture failures: $bad_capture"
    echo "- Missing glyphs: $bad_glyphs"
    echo "- UI smoke failures: $bad_smoke"
    echo "- Scrollable pages: $scroll_pages"
    echo "- Scroll capture failures: $bad_scroll"
    echo "- Button interaction checks: $interaction_checks"
    echo "- Button interaction failures: $bad_interaction"
    echo "- Deprecated feature manifest entries: ${deprecated_manifest_ids:-0}"
    echo "- Deprecated feature interaction entries: ${deprecated_interaction_ids:-0}"
    echo "- Button interaction rule: *action* 类入口必须命中目标文本，不再只按点击动作判定通过"
    echo "- Home title: $home_title"
    echo
  } >>"$report"

  if [[ "$total_pages" -le 0 || "$top_bmp_count" != "$total_pages" ]]; then
    log "page/top screenshot count mismatch: pages=$total_pages top_bmp=$top_bmp_count"
    echo "FAIL: top screenshot count mismatch" >>"$report"
    missing=1
  fi

  if [[ "$bad_capture" -ne 0 ]]; then
    log "capture failures: $bad_capture"
    echo "FAIL: one or more screenshots failed" >>"$report"
    missing=1
  fi

  if [[ "$bad_glyphs" -ne 0 ]]; then
    log "missing glyphs: $bad_glyphs"
    echo "FAIL: Chinese/UI glyph check failed; see glyph_check.tsv" >>"$report"
    missing=1
  fi

  if [[ "$bad_smoke" -ne 0 ]]; then
    log "UI smoke failures: $bad_smoke"
    echo "FAIL: simulator UI smoke check failed; see smoke_check.tsv" >>"$report"
    missing=1
  fi

  if [[ "$bad_scroll" -ne 0 ]]; then
    log "scroll capture failures: $bad_scroll"
    echo "FAIL: simulator scroll check failed; see scroll_check.tsv" >>"$report"
    missing=1
  fi

  if [[ "$bad_interaction" -ne 0 ]]; then
    log "button interaction failures: $bad_interaction"
    echo "FAIL: simulator button interaction check failed; see interaction_check.tsv" >>"$report"
    missing=1
  fi

  if [[ -n "$deprecated_manifest_ids" ]]; then
    log "deprecated feature appears in manifest: $deprecated_manifest_ids"
    echo "FAIL: deprecated Web3 message/test feature appears in manifest: $deprecated_manifest_ids" >>"$report"
    missing=1
  fi

  if [[ -n "$deprecated_interaction_ids" ]]; then
    log "deprecated feature appears in interactions: $deprecated_interaction_ids"
    echo "FAIL: deprecated Web3 message/test feature appears in interactions: $deprecated_interaction_ids" >>"$report"
    missing=1
  fi

  if [[ "$home_title" != "$EXPECTED_HOME_TITLE" ]]; then
    log "unexpected home title: $home_title"
    echo "FAIL: unexpected home title" >>"$report"
    missing=1
  fi

  echo "Key pages:" >>"$report"
  for id in "${KEY_SCREENSHOT_IDS[@]}"; do
    filename="$(awk -F '\t' -v id="$id" 'NR > 1 && $2 == id {print $4; exit}' "$manifest")"
    title="$(awk -F '\t' -v id="$id" 'NR > 1 && $2 == id {print $3; exit}' "$manifest")"
    bmp="$dir/$filename"
    if [[ -s "$bmp" ]]; then
      log "ok: $filename"
      echo "- PASS: $id ($title) -> $filename" >>"$report"
    else
      log "missing or empty key page: $id ($filename)"
      echo "- FAIL: $id ($title) -> $filename" >>"$report"
      missing=1
    fi
  done

  local png_python="${KERN_PNG_PYTHON:-/usr/bin/python3}"
  if [[ ! -x "$png_python" ]]; then
    png_python="$(command -v python3 2>/dev/null || true)"
  fi

  if [[ -z "$png_python" || ! -x "$png_python" ]]; then
    log "skip PNG/contact sheet: python3 not found"
    echo >>"$report"
    echo "WARN: python3 not found; skipped PNG/contact sheet and image checks" >>"$report"
    if (( missing != 0 )); then
      echo >>"$report"
      echo "FINAL: FAIL" >>"$report"
      return 1
    fi
    echo >>"$report"
    echo "FINAL: PASS" >>"$report"
    return 0
  fi

  if "$png_python" - "$dir" "${KEY_SCREENSHOT_IDS[@]}" <<'PY'
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageStat
except ImportError:
    sys.exit(77)

out_dir = Path(sys.argv[1])
keys = sys.argv[2:]
thumbs = []
manifest = out_dir / "manifest.tsv"
rows = {}

for line in manifest.read_text(encoding="utf-8").splitlines()[1:]:
    parts = line.split("\t")
    if len(parts) >= 8:
        rows[parts[1]] = parts

all_bmps = sorted(out_dir.glob("*.bmp"))
if not all_bmps:
    raise SystemExit("no bmp screenshots")

expected_size = None
for bmp in all_bmps:
    with Image.open(bmp) as img:
        rgb = img.convert("RGB")
        if expected_size is None:
            expected_size = rgb.size
        if rgb.size != expected_size:
            raise SystemExit(f"size mismatch: {bmp.name} {rgb.size} != {expected_size}")
        extrema = ImageStat.Stat(rgb).extrema
        if all(lo == hi for lo, hi in extrema):
            raise SystemExit(f"blank screenshot: {bmp.name}")

for key in keys:
    row = rows.get(key)
    if not row:
        raise SystemExit(f"missing key id in manifest: {key}")
    bmp = out_dir / row[3]
    png = out_dir / f"{Path(row[3]).stem}.png"
    with Image.open(bmp) as img:
        rgb = img.convert("RGB")
        rgb.save(png)
        thumb = rgb.copy()
        thumb.thumbnail((240, 240))
        thumbs.append((key, thumb))

def build_sheet(items, output_name, thumb_size=(120, 200), cols=6):
    if not items:
        return

    label_h = 20
    pad = 10
    cell_w = thumb_size[0] + 20
    cell_h = thumb_size[1] + label_h + 18
    cols = min(cols, len(items))
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell_w + pad, rows * cell_h + pad), "white")
    draw = ImageDraw.Draw(sheet)

    for idx, (label, thumb) in enumerate(items):
        col = idx % cols
        row = idx // cols
        x = pad + col * cell_w
        y = pad + row * cell_h
        draw.text((x, y), label[:24], fill=(0, 0, 0))
        sheet.paste(thumb, (x, y + label_h))

    sheet.save(out_dir / output_name)

if thumbs:
    label_h = 24
    pad = 12
    cell_w = 260
    cell_h = 264
    cols = min(3, len(thumbs))
    rows = (len(thumbs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell_w + pad, rows * cell_h + pad), "white")
    draw = ImageDraw.Draw(sheet)

    for idx, (key, thumb) in enumerate(thumbs):
        col = idx % cols
        row = idx // cols
        x = pad + col * cell_w
        y = pad + row * cell_h
        draw.text((x, y), key, fill=(0, 0, 0))
        sheet.paste(thumb, (x, y + label_h))

    sheet.save(out_dir / "contact_sheet_key_pages.png")

top_items = []
bottom_items = []
for bmp in all_bmps:
    with Image.open(bmp) as img:
        rgb = img.convert("RGB")
        thumb = rgb.copy()
        thumb.thumbnail((120, 200))
        item = (bmp.stem, thumb)
        if bmp.stem.endswith("_bottom"):
            bottom_items.append(item)
        else:
            top_items.append(item)

build_sheet(top_items, "contact_sheet_all_top.png")
build_sheet(bottom_items, "contact_sheet_all_bottom.png")
PY
  then
    log "wrote key PNGs and contact sheets"
    echo >>"$report"
    echo "Image checks: PASS" >>"$report"
  else
    local status=$?
    if [[ "$status" -eq 77 ]]; then
      log "skip PNG/contact sheet: Pillow not installed"
      echo >>"$report"
      echo "WARN: Pillow not installed; skipped PNG/contact sheet and image checks" >>"$report"
    else
      log "PNG/contact sheet or image validation failed"
      echo >>"$report"
      echo "FAIL: PNG/contact sheet or image validation failed" >>"$report"
      missing=1
    fi
  fi

  echo >>"$report"
  if (( missing != 0 )); then
    echo "FINAL: FAIL" >>"$report"
    log "acceptance failed: $report"
    return 1
  fi

  echo "FINAL: PASS" >>"$report"
  log "acceptance report: $report"
}

verify_boot_log() {
  local log_file="$1"
  local report="${2:-}"

  if [[ ! -s "$log_file" ]]; then
    log "boot log missing or empty: $log_file"
    [[ -n "$report" ]] && echo "Boot log: FAIL missing or empty" >>"$report"
    return 1
  fi

  if grep -Eiq 'panic|Guru Meditation|assert failed|watchdog|brownout|abort\(|rst:0x10|rst:0xc' "$log_file"; then
    log "boot log FAIL: crash/reset signature found"
    [[ -n "$report" ]] && echo "Boot log: FAIL crash/reset signature found" >>"$report"
    return 1
  fi

  local required=(
    "Display initialized successfully"
    "GT911 found"
    "Setting LCD backlight"
  )
  local token
  for token in "${required[@]}"; do
    if ! grep -Fq "$token" "$log_file"; then
      log "boot log FAIL: missing expected line: $token"
      [[ -n "$report" ]] && echo "Boot log: FAIL missing '$token'" >>"$report"
      return 1
    fi
  done

  log "boot log PASS: $log_file"
  [[ -n "$report" ]] && echo "Boot log: PASS" >>"$report"
  return 0
}

write_final_readiness_file() {
  local release_dir="$1"
  local output_file="$2"
  local release_class="${KERN_RELEASE_CLASS:-development-acceptance}"
  local require_production="${KERN_REQUIRE_PRODUCTION:-0}"
  local firmware_file="$release_dir/kern.bin"
  local acceptance_file="$release_dir/ACCEPTANCE_REPORT.txt"
  local summary_file="$release_dir/RELEASE_SUMMARY.txt"
  local plan_file="$release_dir/PROJECT_PROGRESS_AND_PLAN.md"
  local boot_file="$release_dir/boot.log"
  local production_check_file="$release_dir/PRODUCTION_CHECK.txt"
  local screenshots_dir="$release_dir/screenshots"
  local firmware_sha="missing"
  local bmp_count=0
  local png_count=0
  local button_failures="unknown"
  local missing_glyphs="unknown"
  local final_status="PASS"
  local production_gate_status="FAIL"

  if [[ -s "$firmware_file" ]]; then
    firmware_sha="$(sha256sum "$firmware_file" | awk '{print $1}')"
  else
    final_status="FAIL"
  fi

  if [[ -d "$screenshots_dir" ]]; then
    bmp_count="$(find "$screenshots_dir" -maxdepth 1 -type f -name '*.bmp' | wc -l | tr -d ' ')"
    png_count="$(find "$screenshots_dir" -maxdepth 1 -type f -name '*.png' | wc -l | tr -d ' ')"
  else
    final_status="FAIL"
  fi

  if [[ -s "$acceptance_file" ]]; then
    button_failures="$(awk -F ': ' '/Button interaction failures:/ {print $2; exit}' "$acceptance_file")"
    missing_glyphs="$(awk -F ': ' '/Missing glyphs:/ {print $2; exit}' "$acceptance_file")"
    grep -Fq "FINAL: PASS" "$acceptance_file" || final_status="FAIL"
    grep -Fq -- "- Capture failures: 0" "$acceptance_file" || final_status="FAIL"
    grep -Fq -- "- Missing glyphs: 0" "$acceptance_file" || final_status="FAIL"
    grep -Fq -- "- UI smoke failures: 0" "$acceptance_file" || final_status="FAIL"
    grep -Fq -- "- Scroll capture failures: 0" "$acceptance_file" || final_status="FAIL"
    grep -Fq -- "- Button interaction failures: 0" "$acceptance_file" || final_status="FAIL"
  else
    final_status="FAIL"
  fi

  if [[ -s "$boot_file" ]]; then
    grep -Fq "App version:      $(cat "$ROOT_DIR/version.txt")" "$boot_file" || final_status="FAIL"
    grep -Fq "Display initialized successfully" "$boot_file" || final_status="FAIL"
    grep -Fq "GT911 found" "$boot_file" || final_status="FAIL"
    grep -Fq "Setting LCD backlight" "$boot_file" || final_status="FAIL"
  else
    final_status="FAIL"
  fi

  [[ -s "$summary_file" ]] || final_status="FAIL"
  [[ -s "$plan_file" ]] || final_status="FAIL"

  if [[ -s "$production_check_file" ]] &&
     grep -Fq "PASS: production security config gate" "$production_check_file"; then
    production_gate_status="PASS"
  fi

  if [[ "$require_production" == "1" && "$production_gate_status" != "PASS" ]]; then
    final_status="FAIL"
  fi

  cat >"$output_file" <<EOF
Kern Final Readiness
====================

Time: $(date -Iseconds)
Release directory: $release_dir
Release class: $release_class
App version: $(cat "$ROOT_DIR/version.txt" 2>/dev/null || echo unknown)

Required artifacts:
- Firmware: $firmware_file
- Acceptance report: $acceptance_file
- Project progress and plan: $plan_file
- Boot log: $boot_file
- Production check: $production_check_file
- Screenshots: $screenshots_dir

Verification summary:
- kern.bin SHA256: $firmware_sha
- BMP screenshots: $bmp_count
- PNG screenshots: $png_count
- Missing glyphs: $missing_glyphs
- Button interaction failures: $button_failures
- Acceptance FINAL: $(grep -F "FINAL:" "$acceptance_file" 2>/dev/null | tail -1 || echo missing)
- Boot required tokens: App version, Display initialized successfully, GT911 found, Setting LCD backlight
- Production gate: $production_gate_status

Safety boundary:
- 钱包核心和主要钱包入口已经编译进 ESP32-P4 固件，用于开发验收。
- 核心钱包单测通过，但真实资金版必须先通过创建/导入、公钥/地址、备份/清除、PSBT/消息签名、取消/错误路径和真机回归审查。
- 智能卡 USB CCID、Satochip 和 SeedKeeper 路径已开放开发验证；实机使用必须采用带供电 OTG 转接线或外接供电 Hub。
- 智能卡写卡、PIN/重置、Web3/BTC 签名等敏感路径仍需更完整的真机回归和安全审查后才能进入商业真钱包发布。
- 只有使用 prodship/prodshipflash 且 Production gate: PASS 的包，才允许进入商业真钱包发布流程。

Final readiness: $final_status
EOF

  [[ "$final_status" == "PASS" ]]
}

write_build_provenance_file() {
  local release_dir="$1"
  local output_file="$release_dir/BUILD_PROVENANCE.txt"
  local firmware_file="$release_dir/kern.bin"
  local sdkconfig_file
  local project_desc="$ROOT_DIR/$BUILD_DIR/project_description.json"
  local firmware_sha="missing"
  local git_sha git_dirty cmake_cache target version

  sdkconfig_file="$(production_sdkconfig_path)"
  if [[ -s "$firmware_file" ]]; then
    firmware_sha="$(sha256sum "$firmware_file" | awk '{print $1}')"
  fi

  git_sha="$(cd "$ROOT_DIR" && git rev-parse HEAD 2>/dev/null || echo unknown)"
  if [[ -n "$(cd "$ROOT_DIR" && git status --short 2>/dev/null)" ]]; then
    git_dirty="dirty"
  else
    git_dirty="clean"
  fi

  cmake_cache="$ROOT_DIR/$BUILD_DIR/CMakeCache.txt"
  target="$(awk -F= '/^IDF_TARGET:STRING=/ {print $2; exit}' "$cmake_cache" 2>/dev/null || true)"
  version="$(cat "$ROOT_DIR/version.txt" 2>/dev/null || echo unknown)"

  cat >"$output_file" <<EOF
Kern Build Provenance
=====================

Time: $(date -Iseconds)
Release directory: $release_dir
Release class: ${KERN_RELEASE_CLASS:-development-acceptance}

Firmware:
- File: $firmware_file
- SHA256: $firmware_sha
- App version: $version

Source:
- Git commit: $git_sha
- Worktree: $git_dirty

Build:
- Build dir: $ROOT_DIR/$BUILD_DIR
- SDK config: $sdkconfig_file
- IDF target: ${target:-unknown}
- Project description: $project_desc

Security note:
- This file records build provenance only.
- It does not prove eFuse provisioning, Secure Boot signing, Flash Encryption, or NVS key handling.
- Commercial real-funds release still requires PRODUCTION_CHECK.txt PASS plus manufacturing evidence.
EOF
}

write_manufacturing_readiness_file() {
  local release_dir="$1"
  local output_file="$release_dir/MANUFACTURING_READINESS.txt"
  local production_gate="FAIL"

  if [[ -s "$release_dir/PRODUCTION_CHECK.txt" ]] &&
     grep -Fq "PASS: production security config gate" "$release_dir/PRODUCTION_CHECK.txt"; then
    production_gate="PASS"
  fi

  cat >"$output_file" <<EOF
Kern Manufacturing Readiness
============================

Production gate: $production_gate

必须具备的生产证据：
- signed bootloader 和 signed app 的签名校验记录
- partition table、boot_app0、bootloader、app 合并后的完整 factory image
- Secure Boot V2 配置和签名密钥指纹
- Flash Encryption 配置、烧录后状态和回读限制记录
- NVS Encryption key 生成、保存、加密和访问控制记录
- eFuse summary：设备序列号、批次、Secure Boot、Flash Encryption、JTAG/下载口限制
- 烧录后真机启动日志和固件 SHA256 对应关系
- 生产设备不可逆操作的人工复核记录

当前包的边界：
- 开发验收包可以用于功能测试和 UI 验收。
- 生产候选包即使 Production gate PASS，也必须补齐上面的制造证据后才能进入商业真钱包发布。
- app-only 刷机不等于商业生产烧录。
EOF
}

verify_release_package() {
  local release_dir="${1:-}"
  local failures=0
  local expected_version
  local summary_sha
  local actual_sha

  if [[ -z "$release_dir" ]]; then
    release_dir="$(latest_release_dir)"
  fi

  if [[ -z "$release_dir" || ! -d "$release_dir" ]]; then
    log "final verify failed: release directory not found"
    return 1
  fi

  log "final verify release: $release_dir"

  local required=(
    "kern.bin"
    "README_FIRST.txt"
    "FLASH_COMMANDS.txt"
    "flash_app_linux.sh"
    "flash_app_windows.ps1"
    "RELEASE_INDEX.tsv"
    "ACCEPTANCE_REPORT.txt"
    "RELEASE_SUMMARY.txt"
    "PRODUCTION_CHECK.txt"
    "PROJECT_PROGRESS_AND_PLAN.md"
    "DELIVERY_STATUS.md"
    "MORNING_HANDOVER.md"
    "FINAL_READINESS.txt"
    "BUILD_PROVENANCE.txt"
    "MANUFACTURING_READINESS.txt"
    "SHA256SUMS.txt"
    "boot.log"
    "contact_sheet_key_pages.png"
    "screenshots/manifest.tsv"
    "screenshots/glyph_check.tsv"
    "screenshots/smoke_check.tsv"
    "screenshots/scroll_check.tsv"
    "screenshots/interaction_check.tsv"
    "screenshots/contact_sheet_all_top.png"
    "screenshots/contact_sheet_all_bottom.png"
  )

  local rel
  for rel in "${required[@]}"; do
    if [[ ! -s "$release_dir/$rel" ]]; then
      log "final verify FAIL: missing $rel"
      failures=1
    fi
  done

  if [[ -s "$release_dir/SHA256SUMS.txt" ]]; then
    if (cd "$release_dir" && sha256sum -c SHA256SUMS.txt >/tmp/kern_final_sha256.log); then
      log "final verify: SHA256SUMS PASS"
    else
      log "final verify FAIL: SHA256SUMS mismatch"
      failures=1
    fi
  fi

  if [[ -s "$release_dir/ACCEPTANCE_REPORT.txt" ]]; then
    grep -Fq "FINAL: PASS" "$release_dir/ACCEPTANCE_REPORT.txt" || failures=1
    grep -Fq -- "- Missing glyphs: 0" "$release_dir/ACCEPTANCE_REPORT.txt" || failures=1
    grep -Fq -- "- UI smoke failures: 0" "$release_dir/ACCEPTANCE_REPORT.txt" || failures=1
    grep -Fq -- "- Scroll capture failures: 0" "$release_dir/ACCEPTANCE_REPORT.txt" || failures=1
    grep -Fq -- "- Button interaction failures: 0" "$release_dir/ACCEPTANCE_REPORT.txt" || failures=1
  fi

  if [[ -s "$release_dir/boot.log" ]]; then
    verify_boot_log "$release_dir/boot.log" || failures=1
    expected_version="$(cat "$ROOT_DIR/version.txt" 2>/dev/null || echo unknown)"
    grep -Fq "App version:      $expected_version" "$release_dir/boot.log" || {
      log "final verify FAIL: boot log app version mismatch"
      failures=1
    }
  fi

  if [[ -s "$release_dir/kern.bin" && -s "$release_dir/RELEASE_SUMMARY.txt" ]]; then
    actual_sha="$(sha256sum "$release_dir/kern.bin" | awk '{print $1}')"
    summary_sha="$(awk -F ': ' '/kern.bin SHA256:/ {print $2; exit}' "$release_dir/RELEASE_SUMMARY.txt")"
    if [[ "$actual_sha" != "$summary_sha" ]]; then
      log "final verify FAIL: kern.bin SHA mismatch"
      failures=1
    fi
  fi

  if [[ -s "$release_dir/FINAL_READINESS.txt" ]]; then
    grep -Fq "Final readiness: PASS" "$release_dir/FINAL_READINESS.txt" || failures=1
    if [[ "${KERN_REQUIRE_PRODUCTION:-0}" == "1" ]]; then
      grep -Fq "Production gate: PASS" "$release_dir/FINAL_READINESS.txt" || failures=1
    fi
  fi

  if [[ -s "$release_dir/BUILD_PROVENANCE.txt" ]]; then
    grep -Fq "SHA256:" "$release_dir/BUILD_PROVENANCE.txt" || failures=1
    grep -Fq "Git commit:" "$release_dir/BUILD_PROVENANCE.txt" || failures=1
  fi

  if [[ -s "$release_dir/MANUFACTURING_READINESS.txt" &&
        "${KERN_REQUIRE_PRODUCTION:-0}" == "1" ]]; then
    grep -Fq "Production gate: PASS" "$release_dir/MANUFACTURING_READINESS.txt" || failures=1
  fi

  if (( failures != 0 )); then
    log "final verify: FAIL"
    return 1
  fi

  log "final verify: PASS"
}

write_readme_first_file() {
  local release_dir="$1"
  local output_file="$release_dir/README_FIRST.txt"

  cat >"$output_file" <<EOF
Kern/Krux 先看这个
===================

这是 Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 的 Kern/Krux 真钱包功能版。

最快确认：
1. 在仓库根目录执行：tools/kern_delivery.sh final
2. 看到 final verify: PASS，说明这个交付包、固件 SHA256、截图、按钮验收、启动日志和文档都完整。
3. 如果需要重刷真机，按 FLASH_COMMANDS.txt 操作。

重要文件：
- FINAL_READINESS.txt：最终交付状态。
- ACCEPTANCE_REPORT.txt：模拟器截图、缺字、滚动、按钮导航验收。
- RELEASE_SUMMARY.txt：固件 SHA256、截图数量、启动日志状态。
- PRODUCTION_CHECK.txt：商业真钱包生产门槛检查结果。
- PROJECT_PROGRESS_AND_PLAN.md：当前项目总进展、详细计划和后续阶段。
- boot.log：真机启动日志。
- contact_sheet_key_pages.png：关键页面拼图。
- screenshots/contact_sheet_all_top.png：全部首屏拼图。
- screenshots/contact_sheet_all_bottom.png：全部可滚动页面底部拼图。

安全边界：
- 钱包核心和真钱包入口已经编译进真机固件，用于真机流程验收。
- 这版还不是可以直接放真钱的生产审计固件；主网资金必须等真机全流程、安全审查和回归测试通过。
- 智能卡 APDU、USB CCID 和卡片签名仍未开放。
- 验收时优先使用测试助记词、普通文本、URL、公开测试二维码和空白/FAT32 存储卡。
- `ship` 生成的是开发验收包；商业发布必须使用 `prodship` 并看到 Production gate: PASS。
EOF
}

write_flash_commands_file() {
  local release_dir="$1"
  local output_file="$release_dir/FLASH_COMMANDS.txt"

  cat >"$output_file" <<EOF
Kern/Krux 刷机与校验命令
=========================

进入项目目录：
cd /home/ak/123/Kern

最终校验最新交付包：
tools/kern_delivery.sh final

重新生成开发验收包并自动最终校验：
JOBS=2 tools/kern_delivery.sh ship

检查商业真钱包生产安全门槛：
tools/kern_delivery.sh prodcheck

生成商业发布候选包：
JOBS=2 tools/kern_delivery.sh prodship

app-only 刷写当前开发板并抓启动日志：
JOBS=2 ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh appflash

刷写真机、抓启动日志、重新生成开发验收包并最终校验：
JOBS=2 ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh shipflash

只观察启动日志：
ESPPORT=/dev/ttyACM0 tools/kern_delivery.sh monitor

注意：
- kern.bin 是 app 分区升级固件，不是空白板完整 factory 镜像。
- Linux 可直接运行 release 包内的 ./flash_app_linux.sh；Windows 可右键 PowerShell 运行 flash_app_windows.ps1。
- 两个脚本都会先校验 kern.bin SHA256，校验失败会拒绝刷机。
- 稳定优先，默认刷写速度使用 115200。
- 当前是真钱包功能版，但还不是生产资金审计版；不要用真实资产做验收。
- `ship/shipflash` 只代表开发验收通过，不代表生产资金版。
- 商业真钱包发布必须执行 `prodship/prodshipflash`，并完成 Secure Boot、Flash Encryption、NVS 加密和 eFuse 流程。
- 钱包流程只使用测试助记词、测试 PSBT 或公开测试数据。
EOF
}

write_flash_script_files() {
  local release_dir="$1"
  local firmware_sha="$2"

  if [[ "${KERN_REQUIRE_PRODUCTION:-0}" == "1" ]]; then
    cat >"$release_dir/flash_app_linux.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

echo "生产候选包禁止 app-only 刷机。" >&2
echo "商业真钱包必须走完整生产烧录流程：签名 bootloader、分区表、app、Secure Boot、Flash Encryption、NVS key 和 eFuse 记录。" >&2
exit 1
EOF
    chmod +x "$release_dir/flash_app_linux.sh"

    cat >"$release_dir/flash_app_windows.ps1" <<'EOF'
$ErrorActionPreference = "Stop"
Write-Error "生产候选包禁止 app-only 刷机。商业真钱包必须走完整生产烧录流程：签名 bootloader、分区表、app、Secure Boot、Flash Encryption、NVS key 和 eFuse 记录。"
exit 1
EOF
    return
  fi

  cat >"$release_dir/flash_app_linux.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail

PORT="\${1:-\${ESPPORT:-/dev/ttyACM0}}"
BAUD="\${ESPBAUD:-115200}"
EXPECTED_SHA="$firmware_sha"

cd "\$(dirname "\$0")"

if ! command -v sha256sum >/dev/null 2>&1; then
  echo "缺少 sha256sum，拒绝刷机。" >&2
  exit 1
fi

ACTUAL_SHA="\$(sha256sum kern.bin | awk '{print \$1}')"
if [[ "\$ACTUAL_SHA" != "\$EXPECTED_SHA" ]]; then
  echo "kern.bin SHA256 不匹配，拒绝刷机。" >&2
  echo "期望: \$EXPECTED_SHA" >&2
  echo "实际: \$ACTUAL_SHA" >&2
  exit 1
fi

echo "SHA256 校验通过。"
echo "刷写 app 分区: \$PORT @ \$BAUD"
python3 -m esptool --chip esp32p4 -p "\$PORT" -b "\$BAUD" \\
  --before default_reset --after hard_reset write_flash 0x20000 kern.bin
EOF
  chmod +x "$release_dir/flash_app_linux.sh"

  cat >"$release_dir/flash_app_windows.ps1" <<EOF
\$ErrorActionPreference = "Stop"

param(
  [string]\$Port = \$env:ESPPORT,
  [string]\$Baud = \$env:ESPBAUD
)

if ([string]::IsNullOrWhiteSpace(\$Port)) {
  Write-Error "请指定串口，例如：.\\flash_app_windows.ps1 -Port COM7"
  exit 1
}
if ([string]::IsNullOrWhiteSpace(\$Baud)) { \$Baud = "115200" }

\$ExpectedSha = "$firmware_sha"
\$ScriptDir = Split-Path -Parent \$MyInvocation.MyCommand.Path
Set-Location \$ScriptDir

if (-not (Test-Path ".\\kern.bin")) {
  Write-Error "找不到 kern.bin，拒绝刷机。"
  exit 1
}

\$ActualSha = (Get-FileHash ".\\kern.bin" -Algorithm SHA256).Hash.ToLowerInvariant()
if (\$ActualSha -ne \$ExpectedSha.ToLowerInvariant()) {
  Write-Error "kern.bin SHA256 不匹配，拒绝刷机。`n期望: \$ExpectedSha`n实际: \$ActualSha"
  exit 1
}

Write-Host "SHA256 校验通过。"
Write-Host "刷写 app 分区: \$Port @ \$Baud"

\$PythonCmd = "py"
& \$PythonCmd -3 -m esptool version *> \$null
if (\$LASTEXITCODE -eq 0) {
  & \$PythonCmd -3 -m esptool --chip esp32p4 -p \$Port -b \$Baud --before default_reset --after hard_reset write_flash 0x20000 kern.bin
  if (\$LASTEXITCODE -ne 0) { exit \$LASTEXITCODE }
} else {
  \$PythonCmd = "python"
  & \$PythonCmd -m esptool --chip esp32p4 -p \$Port -b \$Baud --before default_reset --after hard_reset write_flash 0x20000 kern.bin
  if (\$LASTEXITCODE -ne 0) { exit \$LASTEXITCODE }
}
EOF
}

write_release_index_file() {
  local release_dir="$1"
  local output_file="$release_dir/RELEASE_INDEX.tsv"

  cat >"$output_file" <<EOF
path	description
README_FIRST.txt	先看这个，中文快速入口
FLASH_COMMANDS.txt	刷机、最终校验和重新打包命令
flash_app_linux.sh	Linux 开发验收刷机脚本；生产候选包会拒绝 app-only 刷机
flash_app_windows.ps1	Windows 开发验收刷机脚本；生产候选包会拒绝 app-only 刷机
FINAL_READINESS.txt	最终交付状态和安全边界
RELEASE_SUMMARY.txt	固件 SHA256、截图数量、启动日志状态
PRODUCTION_CHECK.txt	商业真钱包生产门槛检查结果
BUILD_PROVENANCE.txt	构建来源、固件 SHA256、sdkconfig 和 git 状态
MANUFACTURING_READINESS.txt	生产烧录、签名、eFuse 和制造证据清单
PROJECT_PROGRESS_AND_PLAN.md	项目总进展、完整详细计划、风险边界和后续阶段
ACCEPTANCE_REPORT.txt	模拟器截图、缺字、UI 烟测、滚动和按钮导航验收
DELIVERY_STATUS.md	完整交付范围、风险边界和验收清单
MORNING_HANDOVER.md	明早实机验收顺序
SHA256SUMS.txt	交付包内所有文件的 SHA256
boot.log	最新真机启动日志
kern.bin	ESP32-P4 app 分区固件
contact_sheet_key_pages.png	关键页面拼图
screenshots/contact_sheet_all_top.png	全部页面首屏拼图
screenshots/contact_sheet_all_bottom.png	全部可滚动页面底部拼图
screenshots/manifest.tsv	页面清单与截图文件映射
screenshots/glyph_check.tsv	中文/UI 缺字检查
screenshots/smoke_check.tsv	UI 标签和可点击对象烟测
screenshots/scroll_check.tsv	可滚动页面底部截图验收
screenshots/interaction_check.tsv	按钮导航验收
EOF
}

package_release() {
  local stamp release_dir screenshot_dir firmware_src firmware_dst contact_src contact_dst
  local bmp_count png_count firmware_sha git_sha git_dirty acceptance_src acceptance_dst
  local status_doc_src status_doc_dst handover_doc_src handover_doc_dst plan_doc_src plan_doc_dst
  local boot_log_src boot_log_dst boot_log_status readiness_dst production_check_dst production_gate_status

  stamp="$(date +%Y%m%d_%H%M%S)"
  release_dir="$ROOT_DIR/_release/kern_delivery_$stamp"
  firmware_src="$ROOT_DIR/$BUILD_DIR/kern.bin"
  firmware_dst="$release_dir/kern.bin"

  log "prepare non-flashing release package: $release_dir"
  bake_fonts
  build_sim
  screenshots
  screenshot_dir="$LAST_SCREENSHOT_DIR"
  verify_screenshots "$screenshot_dir"
  build_firmware

  mkdir -p "$release_dir"

  production_check_dst="$release_dir/PRODUCTION_CHECK.txt"
  if production_check >"$production_check_dst" 2>&1; then
    production_gate_status="PASS"
  else
    production_gate_status="FAIL"
  fi
  log "production gate for package: $production_gate_status ($production_check_dst)"

  if [[ -s "$firmware_src" ]]; then
    cp -f "$firmware_src" "$firmware_dst"
    log "copied firmware: $firmware_dst"
    firmware_sha="$(sha256sum "$firmware_dst" | awk '{print $1}')"
  else
    log "firmware not found: $firmware_src"
    log "run '$0 build' first if firmware binary is required"
    firmware_dst="not packaged; missing $firmware_src"
    firmware_sha="not available"
  fi

  if [[ -d "$screenshot_dir" ]]; then
    cp -a "$screenshot_dir" "$release_dir/screenshots"
    log "copied screenshots: $release_dir/screenshots"
  fi

  status_doc_src="$ROOT_DIR/docs/DELIVERY_STATUS.md"
  status_doc_dst="$release_dir/DELIVERY_STATUS.md"
  if [[ -s "$status_doc_src" ]]; then
    cp -f "$status_doc_src" "$status_doc_dst"
  else
    status_doc_dst="not packaged; missing $status_doc_src"
  fi

  handover_doc_src="$ROOT_DIR/docs/MORNING_HANDOVER.md"
  handover_doc_dst="$release_dir/MORNING_HANDOVER.md"
  if [[ -s "$handover_doc_src" ]]; then
    cp -f "$handover_doc_src" "$handover_doc_dst"
  else
    handover_doc_dst="not packaged; missing $handover_doc_src"
  fi

  plan_doc_src="$ROOT_DIR/docs/PROJECT_PROGRESS_AND_PLAN.md"
  plan_doc_dst="$release_dir/PROJECT_PROGRESS_AND_PLAN.md"
  if [[ -s "$plan_doc_src" ]]; then
    cp -f "$plan_doc_src" "$plan_doc_dst"
  else
    plan_doc_dst="not packaged; missing $plan_doc_src"
  fi

  boot_log_src="$(find "$ROOT_DIR/docs/logs" -maxdepth 1 -type f -name 'boot_*.log' -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR == 1 {print $2}')"
  boot_log_dst="$release_dir/boot.log"
  if [[ -n "$boot_log_src" && -s "$boot_log_src" ]]; then
    cp -f "$boot_log_src" "$boot_log_dst"
    if verify_boot_log "$boot_log_dst"; then
      boot_log_status="PASS"
    else
      boot_log_status="FAIL"
    fi
  else
    boot_log_dst="not packaged; no boot log found"
    boot_log_status="not available"
  fi

  acceptance_src="$screenshot_dir/ACCEPTANCE_REPORT.txt"
  acceptance_dst="$release_dir/ACCEPTANCE_REPORT.txt"
  if [[ -s "$acceptance_src" ]]; then
    cp -f "$acceptance_src" "$acceptance_dst"
  else
    acceptance_dst="not generated; see $acceptance_src"
  fi

  contact_src="$screenshot_dir/contact_sheet_key_pages.png"
  contact_dst="$release_dir/contact_sheet_key_pages.png"
  if [[ -s "$contact_src" ]]; then
    cp -f "$contact_src" "$contact_dst"
  else
    contact_dst="not generated; see $contact_src"
  fi

  bmp_count="$(find "$screenshot_dir" -maxdepth 1 -type f -name '*.bmp' 2>/dev/null | wc -l | tr -d ' ')"
  png_count="$(find "$screenshot_dir" -maxdepth 1 -type f -name '*.png' 2>/dev/null | wc -l | tr -d ' ')"
  git_sha="$(cd "$ROOT_DIR" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  if [[ -n "$(cd "$ROOT_DIR" && git status --short 2>/dev/null)" ]]; then
    git_dirty="dirty"
  else
    git_dirty="clean"
  fi

  cat >"$release_dir/RELEASE_SUMMARY.txt" <<EOF
Kern Delivery Release
=====================

Time: $(date -Iseconds)
Release directory: $release_dir

Firmware:
- Source: $firmware_src
- Packaged: $firmware_dst
- kern.bin SHA256: $firmware_sha

Source:
- Git commit: $git_sha
- Worktree: $git_dirty

Screenshots:
- Source: $screenshot_dir
- Packaged directory: $release_dir/screenshots
- Key contact sheet: $contact_dst
- BMP files: $bmp_count
- PNG files: $png_count

Acceptance:
- Report: $acceptance_dst
- Project progress and plan: $plan_doc_dst
- Delivery status: $status_doc_dst
- Latest boot log: $boot_log_dst
- Boot log status: $boot_log_status
- Production check: $production_check_dst
- Production gate: $production_gate_status
- Morning handover: $handover_doc_dst
- Final readiness: $release_dir/FINAL_READINESS.txt
- Quick start: $release_dir/README_FIRST.txt
- Flash commands: $release_dir/FLASH_COMMANDS.txt

Suggested commands:
- Re-run non-flashing checks: tools/kern_delivery.sh check
- Build firmware binary: tools/kern_delivery.sh build
- Check commercial production security gate: tools/kern_delivery.sh prodcheck
- Create development acceptance package: tools/kern_delivery.sh release
- Verify latest package: tools/kern_delivery.sh final
- One-command development package + final verify: tools/kern_delivery.sh ship
- One-command production candidate package + final verify: tools/kern_delivery.sh prodship
- One-command app flash + development package + final verify: ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh shipflash
- One-command app flash + production candidate package + final verify: ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh prodshipflash
- Flash app partition only when explicitly ready: ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh appflash
EOF

  readiness_dst="$release_dir/FINAL_READINESS.txt"
  if write_final_readiness_file "$release_dir" "$readiness_dst"; then
    log "wrote final readiness: $readiness_dst"
  else
    log "wrote final readiness with FAIL status: $readiness_dst"
  fi

  write_readme_first_file "$release_dir"
  write_flash_commands_file "$release_dir"
  write_flash_script_files "$release_dir" "$firmware_sha"
  write_build_provenance_file "$release_dir"
  write_manufacturing_readiness_file "$release_dir"
  write_release_index_file "$release_dir"
  log "wrote quick start files"

  if command -v sha256sum >/dev/null 2>&1; then
    (cd "$release_dir" && find . -type f ! -name SHA256SUMS.txt -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS.txt)
    log "wrote checksums: $release_dir/SHA256SUMS.txt"
  fi

  local archive_path archive_sha
  archive_path="$release_dir.tar.gz"
  tar -czf "$archive_path" -C "$ROOT_DIR/_release" "$(basename "$release_dir")"
  archive_sha="$(sha256sum "$archive_path" | awk '{print $1}')"
  cat >"$ROOT_DIR/_release/LATEST_RELEASE.txt" <<EOF
Recommended delivery package:
$release_dir

Archive:
$archive_path

Firmware:
$firmware_dst

Project plan:
$release_dir/PROJECT_PROGRESS_AND_PLAN.md

kern.bin SHA256:
$firmware_sha

Archive SHA256:
$archive_sha

Status:
ACCEPTANCE_REPORT FINAL: PASS
Boot log status: $boot_log_status
Production gate: $production_gate_status
Final readiness: $(awk -F ': ' '/Final readiness:/ {print $2; exit}' "$readiness_dst" 2>/dev/null || echo unknown)
App version: $(cat "$ROOT_DIR/version.txt" 2>/dev/null || echo unknown)
EOF
  log "wrote latest release pointer: $ROOT_DIR/_release/LATEST_RELEASE.txt"
  log "wrote archive: $archive_path"
  log "wrote summary: $release_dir/RELEASE_SUMMARY.txt"
}

ship_release() {
  KERN_RELEASE_CLASS="${KERN_RELEASE_CLASS:-development-acceptance}" \
  package_release
  verify_release_package "$(latest_release_dir)"
}

ship_flash_release() {
  export KERN_RELEASE_CLASS="${KERN_RELEASE_CLASS:-development-acceptance}"
  bake_fonts
  build_firmware
  app_flash_firmware
  monitor_boot
  package_release
  verify_release_package "$(latest_release_dir)"
}

prod_ship_release() {
  production_check
  KERN_RELEASE_CLASS=production-candidate KERN_REQUIRE_PRODUCTION=1 package_release
  KERN_REQUIRE_PRODUCTION=1 verify_release_package "$(latest_release_dir)"
}

prod_ship_flash_release() {
  production_check
  if [[ "${KERN_ALLOW_PRODUCTION_APPFLASH:-0}" != "1" ]]; then
    log "production app-only flashing is blocked"
    log "use the reviewed production provisioning flow instead, or set KERN_ALLOW_PRODUCTION_APPFLASH=1 only for controlled lab recovery"
    return 1
  fi
  export KERN_RELEASE_CLASS=production-candidate
  export KERN_REQUIRE_PRODUCTION=1
  bake_fonts
  build_firmware
  app_flash_firmware
  monitor_boot
  package_release
  verify_release_package "$(latest_release_dir)"
}

build_firmware() {
  log "build ESP32-P4 firmware"
  source_idf
  (cd "$ROOT_DIR" && cmake --build "$BUILD_DIR" -- -j"$JOBS")
}

flash_firmware() {
  if [[ ! -e "$ESPPORT" ]]; then
    log "flash failed: port not found: $ESPPORT"
    return 1
  fi

  log "flash full firmware on $ESPPORT at $ESPBAUD"
  source_idf
  (cd "$ROOT_DIR" && ESPPORT="$ESPPORT" ESPBAUD="$ESPBAUD" cmake --build "$BUILD_DIR" --target flash -- -j"$JOBS")
}

app_flash_firmware() {
  if [[ ! -e "$ESPPORT" ]]; then
    log "app-flash failed: port not found: $ESPPORT"
    return 1
  fi

  log "flash app only on $ESPPORT at $ESPBAUD"
  source_idf
  (cd "$ROOT_DIR" && ESPPORT="$ESPPORT" ESPBAUD="$ESPBAUD" cmake --build "$BUILD_DIR" --target app-flash -- -j"$JOBS")
}

monitor_boot() {
  local log_file="${1:-}"
  if [[ ! -e "$ESPPORT" ]]; then
    log "monitor failed: port not found: $ESPPORT"
    return 1
  fi

  if [[ -z "$log_file" ]]; then
    mkdir -p "$ROOT_DIR/docs/logs"
    log_file="$ROOT_DIR/docs/logs/boot_$(date +%Y%m%d_%H%M%S).log"
  fi
  LAST_BOOT_LOG="$log_file"

  log "capture boot log for 14 seconds: $log_file"
  if command -v python3 >/dev/null 2>&1; then
    if python3 - "$ESPPORT" "$ESPBAUD" <<'PY' | tee "$log_file"
import sys
import time

try:
    import serial
except ImportError:
    sys.exit(77)

port = sys.argv[1]
baud = int(sys.argv[2])
with serial.Serial(port, baud, timeout=0.2) as ser:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.12)
    ser.rts = False
    time.sleep(0.2)
    end = time.time() + 14
    while time.time() < end:
        data = ser.read(4096)
        if data:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
PY
    then
      verify_boot_log "$log_file"
      return $?
    fi
    local status=$?
    if [[ "$status" -ne 77 ]]; then
      log "serial boot log failed with status $status"
    else
      log "pyserial not installed; falling back to idf.py monitor"
    fi
  fi

  source_idf
  (cd "$ROOT_DIR" && timeout 14s idf.py -B "$BUILD_DIR" -p "$ESPPORT" monitor || true) | tee "$log_file"
  verify_boot_log "$log_file"
}

production_check() {
  "$ROOT_DIR/tools/kern_production_check.sh" "$(production_sdkconfig_path)"
}

case "$ACTION" in
  fonts)
    bake_fonts
    ;;
  sim)
    bake_fonts
    build_sim
    screenshots
    ;;
  check)
    bake_fonts
    build_sim
    screenshots
    verify_screenshots "$LAST_SCREENSHOT_DIR"
    ;;
  verify)
    verify_screenshots "$VERIFY_DIR"
    ;;
  build)
    bake_fonts
    build_firmware
    ;;
  release|package)
    package_release
    ;;
  ship)
    ship_release
    ;;
  shipflash)
    ship_flash_release
    ;;
  prodship|production-ship)
    prod_ship_release
    ;;
  prodshipflash|production-shipflash)
    prod_ship_flash_release
    ;;
  final|final-verify)
    verify_release_package "$VERIFY_DIR"
    ;;
  flash)
    bake_fonts
    build_firmware
    flash_firmware
    monitor_boot
    ;;
  appflash)
    bake_fonts
    build_firmware
    app_flash_firmware
    monitor_boot
    ;;
  monitor)
    monitor_boot
    ;;
  prodcheck|production-check)
    production_check
    ;;
  all)
    bake_fonts
    build_sim
    screenshots
    verify_screenshots "$LAST_SCREENSHOT_DIR"
    build_firmware
    flash_firmware
    monitor_boot
    ;;
  *)
    echo "Usage: $0 [fonts|sim|check|verify [screenshot-dir]|build|release|package|ship|shipflash|prodship|prodshipflash|final [release-dir]|flash|appflash|monitor|prodcheck|all]" >&2
    exit 2
    ;;
esac
