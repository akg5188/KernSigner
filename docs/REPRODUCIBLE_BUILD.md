# Reproducible Build Notes

## Current Wave43 Image (2026-10-03)

The current **test-only** firmware is in [`firmware/wave_43_20261003/`](../firmware/wave_43_20261003/README.zh-CN.md). The notes below this section describe an older development snapshot; its binary hashes are not applicable to the current source tree.

The 2026-10-03 firmware was fully erased and flashed to an ESP32-P4 Wave43 board. Esptool verified all four writes; a boot log confirmed the factory app at `0x20000`, display initialization and GT911 touch initialization. This does not establish safety for real funds or validate all wallet workflows.

To reproduce this exact image from the repository homepage, run
`bash scripts/rebuild_verified_wave43.sh` after installing ESP-IDF v5.5.4.
The script checks out the pinned source commit
`1b4bd14f6e6701cfee884e95bf21176f9a500233` into a separate ignored
worktree, builds with the pinned environment, compares all four images, and
runs the tests. It stops with a nonzero exit status if any check fails. It
never flashes the board. Use an optional new directory argument to keep
multiple attempts (for example, `bash scripts/rebuild_verified_wave43.sh
build_wave_43_verified_2`). The original working tree is not modified.

Build inputs:

- ESP-IDF v5.5.4, local commit `735507283d5b2f9fb363a1901172dbd9e847945d` (the local checkout had only unrelated example-file modifications).
- `riscv32-esp-elf` toolchain `esp-14.2.0_20260121`; Python 3.13.7.
- `sdkconfig.defaults` and `sdkconfig.defaults.wave_43` with ESP-IDF minimal build enabled by the project CMakeLists. The old `sdkconfig.release.wave_43` is **not** the configuration used for this image.
- Committed `dependencies.lock`, recursive submodules, and the committed `main/ui/assets/signer_cn_20.c` and `signer_cn_28.c` assets. Do not update dependencies or regenerate fonts while verifying this image.
- The new, disabled `ESP_VIDEO_USE_CUSTOMIZED_ESP_H264_VERSION` Kconfig compatibility setting only permits a fresh ESP-IDF component-manager resolution; it does not alter the flashed firmware image.

For manual verification, check out the pinned source commit above, not a later
`master`. Use a clean working tree and ESP-IDF v5.5.4. `just build wave_43`
may reuse an older build directory, so use the fresh-build command below
when comparing hashes.

From the repository root:

```bash
git submodule update --init --recursive
source /path/to/esp-idf-v5.5.4/export.sh
idf.py -B build_wave_43_verify \
  -D SDKCONFIG=build_wave_43_verify/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
  build
(cd firmware/wave_43_20261003 && sha256sum -c SHA256SUMS.txt)
cmp build_wave_43_verify/kernsigner.bin firmware/wave_43_20261003/kernsigner.bin
cmp build_wave_43_verify/bootloader/bootloader.bin firmware/wave_43_20261003/bootloader.bin
cmp build_wave_43_verify/partition_table/partition-table.bin firmware/wave_43_20261003/partition-table.bin
cmp build_wave_43_verify/ota_data_initial.bin firmware/wave_43_20261003/ota_data_initial.bin
./scripts/test.sh
```

The app SHA256 must be `7c08b627af48a46f0e13cdc00759d0b539e2e5edca287c5ce71f192094659dc0`. The four component images were compared byte-for-byte to the images flashed on 2026-10-03. This result applies to the same source commit, dependencies and toolchain, not to arbitrary future code or tool updates. If a comparison fails, do not flash the resulting image as though it were this release.

## Historical Snapshot

Status: **untested development firmware**.

This repository currently contains a development snapshot for Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3 (`wave_43`). The included firmware has been rebuilt locally, passed simulator delivery acceptance, and the app-only image was flashed to a real board with a passing boot-log capture on 2026-05-24. It is **not audited** and must be treated as **test-funds only** until independent real-device and production-security verification is complete.

## Pinned Local Build Inputs

- Board: Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3
- ESP-IDF used for the included firmware: `v5.5.4`
- App version: `0.0.7-rc1`
- Release sdkconfig: `sdkconfig.release.wave_43`
- Dependencies lock: `dependencies.lock`
- Submodules: use `git submodule update --init --recursive`; `components/k_quirc` is pinned to `900b74c694435c31ab7ac4fa5d99cd42fbf109f0` from `https://github.com/akg5188/k_quirc.git`

Included firmware:

- Full one-file image: `firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-full.bin`
- App-only image: `firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin`
- Bootloader, partition table, OTA data, `flash_args`, and `flasher_args.json`
- SHA256 is recorded in `firmware/wave_43/SHA256SUMS.txt`

## Rebuild The Same Firmware Locally

Preferred local command, because it bakes the multilingual font subset before building:

```bash
cd /home/ak/123/Kern
JOBS=2 tools/signer_delivery.sh build
sha256sum build_wave_43_fresh/kernsigner.bin
```

Manual equivalent:

```bash
git clone --recursive https://github.com/akg5188/KernSigner.git
cd KernSigner
git submodule update --init --recursive
source /home/ak/esp-idf-v5.5.4/export.sh
tools/bake_signer_cn_fonts.py
idf.py -B build_wave_43_fresh \
  -D SDKCONFIG=build_wave_43_fresh/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
  build
sha256sum build_wave_43_fresh/kernsigner.bin
sha256sum firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin
```

SHA256 of the older files currently in `firmware/wave_43/` (not the May build hash shown below):

```text
fa87f6d38e7e8f259c0c1065b286f6ae70dafd21e2d52bfa3c345628ea277aab  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin
cda073a2cc766bc34cd9b9dc1aa75eb6164fb9e582ba35113b8ea9cb2889c3be  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-full.bin
```

The historical build commands above and the May acceptance log below refer to an earlier snapshot; neither establishes a byte-for-byte match for the current checkout. Use the 2026-10-03 procedure at the top for the current firmware.

A byte-for-byte match can depend on using the same ESP-IDF, toolchain, submodule commits, generated font assets, and sdkconfig. If the hash differs, compare:

```bash
git status --short
git submodule status --recursive
sha256sum sdkconfig.release.wave_43 dependencies.lock main/ui/assets/signer_cn_20.c main/ui/assets/signer_cn_28.c
```

Latest local acceptance checks for this snapshot:

- `JOBS=2 tools/signer_delivery.sh build`: PASS, app hash `bd50d526089b13d7af360e0ef4514b5e961564138452bb2c5a028f6132dac502`
- `tools/signer_delivery.sh check`: PASS, report `docs/screens/delivery_20260523_175620/ACCEPTANCE_REPORT.txt`
- `./scripts/test.sh`: PASS
- `(cd firmware/wave_43 && sha256sum -c SHA256SUMS.txt)`: PASS
- OKX local sample: static photo + 1 fps video frames `12/12 decoded`
- App-only real-device flash: PASS on `/dev/ttyACM0`, `Hash of data verified`
- Boot-log capture: PASS on local hardware after app-only flashing.

## Flash The Included Firmware

For a first-time board or unknown firmware, flash the full one-file image:

```bash
cd KernSigner
python3 -m esptool --chip esp32p4 -p /dev/ttyACM0 -b 115200 \
  --before default_reset --after hard_reset write_flash 0x0 \
  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-full.bin
```

For a board that already has the expected bootloader and partition table, app-only flashing is also available:

```bash
python3 -m esptool --chip esp32p4 -p /dev/ttyACM0 -b 115200 \
  --before default_reset --after hard_reset write_flash 0x20000 \
  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin
```

## Safety Boundary

- Not a production release.
- Not audited for real funds.
- Use test mnemonics and test funds only.
- Production use requires Secure Boot, Flash Encryption, NVS encryption, debug-port policy, release provenance, and real-device acceptance review.
