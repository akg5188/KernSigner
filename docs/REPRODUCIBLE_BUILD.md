# Reproducible Build Notes

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

Expected included firmware SHA256:

```text
bd50d526089b13d7af360e0ef4514b5e961564138452bb2c5a028f6132dac502  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-app.bin
c31bb74caed08a17313ae0693c2b3a17e09cb3e0f9a2ae7cb7616df3622a282a  firmware/wave_43/kernsigner-wave43-0.0.7-rc1-untested-full.bin
```

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
