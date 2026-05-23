# Reproducible Build Notes

Status: **untested development firmware**.

This repository currently contains a development snapshot for Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3 (`wave_43`). It has been built and flashed locally, but it is **not audited** and must be treated as **test-funds only** until independent real-device and production-security verification is complete.

## Pinned Local Build Inputs

- Board: Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3
- ESP-IDF used for the included firmware: `v5.5.4`
- App version: `0.0.7-rc1`
- Release sdkconfig: `sdkconfig.release.wave_43`
- Dependencies lock: `dependencies.lock`
- Submodules: use `git submodule update --init --recursive`

Included firmware:

- Full one-file image: `firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-full.bin`
- App-only image: `firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin`
- Bootloader, partition table, OTA data, `flash_args`, and `flasher_args.json`
- SHA256 is recorded in `firmware/wave_43/SHA256SUMS.txt`

## Rebuild The Same Firmware Locally

```bash
git clone --recursive https://github.com/akg5188/KernSigner.git
cd KernSigner
git submodule update --init --recursive
source ~/esp/esp-idf-v5.5.4/export.sh
idf.py -B build_wave_43_fresh \
  -D SDKCONFIG=build_wave_43_fresh/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
  build
sha256sum build_wave_43_fresh/kernsigner.bin
sha256sum firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin
```

Expected included firmware SHA256:

```text
dc2ae5caa3a8200c37dcdb7cbaa4d42ef89dfa86219fd45e99581efe1aef5fc1  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin
be1768a4f3897d04dadddab364cb34092e22ecb15c254d6c1c19dea8de27c07a  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-full.bin
```

A byte-for-byte match can depend on using the same ESP-IDF, toolchain, submodule commits, generated font assets, and sdkconfig. If the hash differs, compare:

```bash
git status --short
git submodule status --recursive
sha256sum sdkconfig.release.wave_43 dependencies.lock main/ui/assets/krux_cn_20.c main/ui/assets/krux_cn_28.c
```

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
