#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_ROOT="${BUILD_ROOT:-/tmp/satochip-wallet-ascii-build}"
BUILD_DIR="$BUILD_ROOT/satochip-wallet-android"
GRADLE_USER_HOME="${GRADLE_USER_HOME:-$BUILD_ROOT/.gradle}"
OUT_DIR="${OUT_DIR:-$SRC_DIR/dist}"
VARIANT="${1:-release}"
REQUIRED_JAVA_MAJOR="17"

java_major_version() {
  local java_bin="$1"
  "$java_bin" -version 2>&1 | awk -F '"' '/version/ {split($2, parts, "."); print parts[1]; exit}'
}

is_required_jdk() {
  local candidate="${1:-}"
  local java_bin="$candidate/bin/java"
  local javac_bin="$candidate/bin/javac"
  local major=""
  [[ -x "$java_bin" && -x "$javac_bin" ]] || return 1
  major="$(java_major_version "$java_bin")"
  [[ "$major" == "$REQUIRED_JAVA_MAJOR" ]]
}

detect_java_home() {
  local candidate=""

  find_local_jdk17() {
    local dir
    shopt -s nullglob
    for dir in "$HOME"/.local-jdk/jdk-17*; do
      if [[ -x "$dir/bin/java" && -x "$dir/bin/javac" ]]; then
        printf '%s\n' "$dir"
        return 0
      fi
    done
    return 1
  }

  for candidate in \
    "${JAVA_HOME:-}" \
    "$(find_local_jdk17 || true)"
  do
    if [[ -n "$candidate" ]] && is_required_jdk "$candidate"; then
      printf '%s\n' "$candidate"
      return
    fi
  done

  if command -v javac >/dev/null 2>&1; then
    candidate="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
    if is_required_jdk "$candidate"; then
      printf '%s\n' "$candidate"
      return
    fi
  fi

  if command -v java >/dev/null 2>&1; then
    candidate="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
    if is_required_jdk "$candidate"; then
      printf '%s\n' "$candidate"
      return
    fi
  fi

  echo ""
}

read_sdk_dir_from_properties() {
  local file="$1"
  [[ -f "$file" ]] || return 0
  awk -F= '/^sdk\.dir=/{sub(/^[^=]*=/, ""); print; exit}' "$file"
}

is_valid_sdk_dir() {
  local dir="${1:-}"
  [[ -n "$dir" && -d "$dir" ]]
}

detect_android_sdk() {
  local candidate

  for candidate in \
    "$(read_sdk_dir_from_properties "$SRC_DIR/local.properties")" \
    "${ANDROID_SDK_ROOT:-}" \
    "${ANDROID_HOME:-}" \
    "$HOME/Android/Sdk" \
    "$HOME/Android/sdk" \
    "/usr/lib/android-sdk" \
    "/opt/android-sdk" \
    "/opt/android-sdk-linux"
  do
    if is_valid_sdk_dir "$candidate"; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  echo ""
}

require_sdk_component() {
  local path="$1"
  local description="$2"
  if [[ ! -e "$path" ]]; then
    cat >&2 <<EOF
Missing required Android SDK component: $description
Checked path: $path

Install at least:
  platforms;android-34
  build-tools;35.0.0
  platform-tools
EOF
    exit 1
  fi
}

case "$VARIANT" in
  release)
    GRADLE_TASK=":app:assembleRelease"
    SRC_APK="$BUILD_DIR/app/build/outputs/apk/release/app-release.apk"
    OUT_APK="${OUT_APK:-$OUT_DIR/satochip-wallet-release.apk}"
    ;;
  debug)
    GRADLE_TASK=":app:assembleDebug"
    SRC_APK="$BUILD_DIR/app/build/outputs/apk/debug/app-debug.apk"
    OUT_APK="${OUT_APK:-$OUT_DIR/satochip-wallet-debug.apk}"
    ;;
  *)
    echo "用法: ./scripts/build_local_ascii.sh [release|debug]" >&2
    exit 1
    ;;
esac

GRADLE_JVMARGS="${WALLET_GRADLE_JVMARGS:-}"
GRADLE_WORKERS_MAX="${WALLET_GRADLE_WORKERS_MAX:-}"

SDK_DIR="$(detect_android_sdk)"
if [[ -z "$SDK_DIR" ]]; then
  cat >&2 <<'EOF'
Unable to locate a usable Android SDK.

Checked:
  1. ./local.properties -> sdk.dir
  2. ANDROID_SDK_ROOT
  3. ANDROID_HOME
  4. Common paths like ~/Android/Sdk

Fix it with either:
  cp local.properties.example local.properties
  # then edit sdk.dir=/your/Android/Sdk

Or export:
  export ANDROID_SDK_ROOT=/your/Android/Sdk

Required packages:
  platforms;android-34
  build-tools;35.0.0
  platform-tools
EOF
  exit 1
fi

require_sdk_component "$SDK_DIR/platforms/android-34" "Android SDK Platform 34"
require_sdk_component "$SDK_DIR/build-tools/35.0.0" "Android Build Tools 35.0.0"
require_sdk_component "$SDK_DIR/platform-tools" "Android platform-tools"

mkdir -p "$BUILD_ROOT" "$GRADLE_USER_HOME"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

rsync -a \
  --exclude '.gradle' \
  --exclude 'app/build' \
  --exclude 'dist' \
  --exclude 'local.properties' \
  "$SRC_DIR/" "$BUILD_DIR/"

printf 'sdk.dir=%s\n' "$SDK_DIR" > "$BUILD_DIR/local.properties"

cd "$BUILD_DIR"
export JAVA_HOME="$(detect_java_home)"
if [[ -z "$JAVA_HOME" ]]; then
  echo "Unable to locate a usable JDK $REQUIRED_JAVA_MAJOR with both java and javac" >&2
  exit 1
fi
export GRADLE_USER_HOME
export ORG_GRADLE_JAVA_INSTALLATIONS_PATHS="$JAVA_HOME"
export PATH="$JAVA_HOME/bin:$PATH"
GRADLE_ARGS=(--no-daemon "$GRADLE_TASK" --console=plain)
if [[ -n "$GRADLE_JVMARGS" ]]; then
  GRADLE_ARGS=("-Dorg.gradle.jvmargs=${GRADLE_JVMARGS}" "${GRADLE_ARGS[@]}")
fi
if [[ -n "$GRADLE_WORKERS_MAX" ]]; then
  GRADLE_ARGS=("-Dorg.gradle.workers.max=${GRADLE_WORKERS_MAX}" "${GRADLE_ARGS[@]}")
fi
./gradlew "${GRADLE_ARGS[@]}"

mkdir -p "$OUT_DIR"
cp -f "$SRC_APK" "$OUT_APK"
apk_sha="$(sha256sum "$OUT_APK" | awk '{print $1}')"
printf '%s  %s\n' "$apk_sha" "$(basename "$OUT_APK")" > "$OUT_APK.sha256"

echo "APK written to: $OUT_APK"
