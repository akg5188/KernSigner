#!/usr/bin/env bash
# Rebuild the 2026-10-03 Wave43 test image without changing the current checkout.
set -e

RELEASE_COMMIT=1b4bd14f6e6701cfee884e95bf21176f9a500233
IDF_COMMIT=735507283d5b2f9fb363a1901172dbd9e847945d
TOOLCHAIN_VERSION=esp-14.2.0_20260121
PYTHON_VERSION=3.13.7

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:-"$repo/build_wave_43_verified"}
case "$output" in
  /*) ;;
  *) output="$repo/$output" ;;
esac
source_dir="$output/source"

if [ -e "$output" ]; then
  echo "Output already exists: $output (choose a new directory)" >&2
  exit 1
fi

if ! git -C "$repo" cat-file -e "$RELEASE_COMMIT^{commit}" 2>/dev/null; then
  echo "Fetching the pinned source commit (for shallow clones)..." >&2
  git -C "$repo" fetch --no-tags origin "$RELEASE_COMMIT"
fi

if [ -z "${IDF_PATH:-}" ]; then
  if [ -f "$HOME/esp-idf-v5.5.4/export.sh" ]; then
    IDF_PATH="$HOME/esp-idf-v5.5.4"
  else
    IDF_PATH="$HOME/esp/esp-idf-v5.5.4"
  fi
fi
if [ ! -f "$IDF_PATH/export.sh" ]; then
  echo "ESP-IDF v5.5.4 not found; set IDF_PATH to its directory." >&2
  exit 1
fi
if [ "$(git -C "$IDF_PATH" rev-parse HEAD)" != "$IDF_COMMIT" ]; then
  echo "Wrong ESP-IDF commit; expected $IDF_COMMIT." >&2
  exit 1
fi

# ESP-IDF export activates the Python environment and the pinned toolchain.
source "$IDF_PATH/export.sh"
if [ "$(python --version 2>&1)" != "Python $PYTHON_VERSION" ]; then
  echo "Wrong Python version; expected $PYTHON_VERSION." >&2
  exit 1
fi
compiler=$(command -v riscv32-esp-elf-gcc || true)
case "$compiler" in
  *"/$TOOLCHAIN_VERSION/"*) ;;
  *) echo "Wrong riscv32-esp-elf toolchain; expected $TOOLCHAIN_VERSION." >&2; exit 1 ;;
esac

mkdir -p "$output"
git -C "$repo" worktree add --detach "$source_dir" "$RELEASE_COMMIT"
git -C "$source_dir" submodule update --init --recursive

(
  cd "$source_dir"
  idf.py -B build_wave_43_verify \
    -D SDKCONFIG=build_wave_43_verify/sdkconfig \
    -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
    build
  (cd firmware/wave_43_20261003 && sha256sum -c SHA256SUMS.txt)

  cmp build_wave_43_verify/kernsigner.bin firmware/wave_43_20261003/kernsigner.bin
  echo "PASS: app image matches"
  cmp build_wave_43_verify/bootloader/bootloader.bin firmware/wave_43_20261003/bootloader.bin
  echo "PASS: bootloader image matches"
  cmp build_wave_43_verify/partition_table/partition-table.bin firmware/wave_43_20261003/partition-table.bin
  echo "PASS: partition table image matches"
  cmp build_wave_43_verify/ota_data_initial.bin firmware/wave_43_20261003/ota_data_initial.bin
  echo "PASS: OTA data image matches"
  ./scripts/test.sh
)

printf '\nPASS: all four rebuilt images match the pinned 2026-10-03 firmware.\n'
printf 'Verified app: %s\n' "$source_dir/build_wave_43_verify/kernsigner.bin"
printf 'Original full image: %s\n' "$source_dir/firmware/wave_43_20261003/kernsigner-full.bin"
printf 'This script does not flash any device.\n'
