# Kern

Kern is an experimental ESP32-P4 firmware for air-gapped Bitcoin signing, QR-based wallet workflows, and hardware wallet research. It uses LVGL for the embedded UI, libwally for Bitcoin primitives, and a C codebase tuned for small touch-screen devices.

One of the most important upstream references here is [3rdIteration/SeedSigner](https://github.com/3rdIteration/seedsigner); its wallet flow, UX patterns, and smart-card-adjacent thinking influenced a large part of the project structure.

This repository was largely assembled with AI assistance. It is still unfinished, intended for learning and discussion only, and must not be used to store or sign real funds.

The current tree is a **test-funds validation build**, not an audited production wallet. It contains real wallet paths and Satochip/Web3 work, but production use with mainnet funds requires the security gates and real-device acceptance checks in `docs/` to pass.

## What Works

- Air-gapped Bitcoin wallet flows: load or create a mnemonic, view public keys and addresses, review wallet data, and enter signing flows.
- QR input/output: SeedQR, PSBT/message-signing paths, BBQR/cUR plumbing, text QR generation, and QR classification.
- Mnemonic and backup tooling: manual word entry, numbered imports, grid/1248/Tinyseed/Stackbit-style restore paths, encrypted backup pages, and BIP39 checks.
- Custom derivation: Bitcoin legacy, nested SegWit, native SegWit, Taproot, testnet variants, and EVM address display.
- Satochip smart-card validation paths: USB CCID detection, ATR/status reads, Web3 connection/signing tests, path address display, and BTC watch-only public keys.
- Hardware tooling: display/touch setup, camera preview, storage browser, brightness control, device status, and real-device delivery checks.
- Desktop simulator: runs the LVGL UI in an SDL2 window for UI review and automated screenshots.

## Safety Status

Use this repository with test seeds and test funds only unless you have independently completed the production release gate.

Known release boundaries:

- Not audited for production custody.
- Secure Boot, Flash Encryption, NVS encryption, debug-port closure, and release provenance must be verified before any commercial or mainnet funds build.
- Satochip write-card, PIN-management, reset, SeedKeeper management, and Satochip BTC PSBT/message card-signing features are intentionally not exposed as completed capabilities.
- Web3/Satochip signing is a validation path and still needs broad wallet-format and failure-path regression.

Start with [docs/README.md](docs/README.md) for the full delivery and acceptance map.

## Supported Hardware

Kern supports these Waveshare ESP32-P4 boards:

| Board | Config | Display | Touch | Camera |
| --- | --- | --- | --- | --- |
| [ESP32-P4-WiFi6-Touch-LCD-4B](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm) | `wave_4b` | 720x720 MIPI DSI | GT911 | OV5647 + DW9714 autofocus |
| [ESP32-P4-WiFi6-Touch-LCD-3.5](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-3.5.htm) | `wave_35` | 320x480 SPI | FT5x06 | OV5647 |
| [ESP32-P4-WiFi6-Touch-LCD-5](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-5.htm) | `wave_5` | 720x1280 MIPI DSI | GT911 | OV5647 |
| [ESP32-P4-WiFi6-Touch-LCD-4.3](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm) | `wave_43` | 480x800 MIPI DSI | GT911 | OV5647 |

An OV5647 camera module is required for camera and QR workflows.

ESP32-P4 itself has no Wi-Fi or BLE radio. Some supported development boards include an ESP32-C6 companion chip, but Kern's signer model treats the firmware as an offline, QR-first device.

## Repository Layout

```text
main/core/          Bitcoin, key, PSBT, PIN, storage, EVM, and wallet logic
main/pages/         LVGL page flows and wallet screens
main/ui/            Reusable UI widgets, theme, icons, fonts, and navigation
main/qr/            QR parser, scanner, encoder, and viewer
main/smartcard/     USB CCID and Satochip integration
main/krux_port/     Krux-style shell, hardware probes, and service adapters
components/         ESP-IDF components and third-party libraries
simulator/          SDL2 desktop simulator
scripts/            Format, test, CI, and release helpers
tools/              Delivery, production-check, and asset-baking helpers
docs/               Acceptance plans, security gates, and delivery records
```

## Prerequisites

Kern targets [ESP-IDF v6.0.1](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/get-started/index.html) for ESP32-P4:

```bash
git clone --depth 1 --recurse-submodules --shallow-submodules \
  -b v6.0.1 https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32p4
. ~/esp/esp-idf/export.sh
```

Simulator builds also need SDL2 and mbedTLS:

```bash
# Debian/Ubuntu
sudo apt install build-essential cmake libsdl2-dev libmbedtls-dev

# macOS
brew install cmake sdl2 mbedtls
```

## Clone

This repository uses submodules:

```bash
git clone --recursive https://github.com/odudex/Kern.git
cd Kern
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build Firmware

Select the board by passing the matching `sdkconfig.defaults.*` file. Use a separate build directory per board:

```bash
# wave_4b
idf.py -B build_wave_4b \
  -D SDKCONFIG=build_wave_4b/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_4b' \
  build

# wave_35
idf.py -B build_wave_35 \
  -D SDKCONFIG=build_wave_35/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_35' \
  build

# wave_5
idf.py -B build_wave_5 \
  -D SDKCONFIG=build_wave_5/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_5' \
  build

# wave_43
idf.py -B build_wave_43 \
  -D SDKCONFIG=build_wave_43/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
  build
```

Flash and monitor with ESP-IDF:

```bash
idf.py -B build_wave_43 -p /dev/ttyACM0 flash monitor
```

When changing board configs in the same build directory, clean first:

```bash
idf.py -B build_wave_43 fullclean
```

## Desktop Simulator

The simulator renders the LVGL UI in an SDL2 window:

```bash
cd simulator
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j"$(nproc)"
./build/kern_simulator
```

Useful options:

```bash
./build/kern_simulator --width 480 --height 800
./build/kern_simulator --qr-image path/to/qr.png
./build/kern_simulator --qr-dir path/to/qr-images
./build/kern_simulator --data-dir /tmp/kern-sim-data
```

See [simulator/README.md](simulator/README.md) for webcam support and platform notes.

## Test And Check

Run unit tests:

```bash
./scripts/test.sh
```

Run formatting:

```bash
./scripts/format.sh
./scripts/format.sh --check
```

Run the local CI bundle used by commit-history checks:

```bash
./scripts/ci-checks.sh
```

Run the delivery and production checks used by the current validation workflow:

```bash
tools/kern_delivery.sh check
tools/kern_delivery.sh final
tools/kern_delivery.sh prodcheck
```

`prodcheck` is expected to fail on normal development builds until production security options and release provenance are deliberately enabled.

## Release Packages

`scripts/release.sh` builds release zips for all supported boards. It reads the version from `version.txt` and writes packages under `release/v<version>/`:

```bash
./scripts/release.sh
```

Pre-release firmware, when published, is for testing. Do not use pre-release builds for real savings or commercial custody.

Merged binary flashing:

```bash
esptool --chip esp32p4 --baud 460800 write-flash 0x0 kern-v0.0.7-rc1.bin
```

To preserve NVS data, flash individual binaries instead:

```bash
esptool --chip esp32p4 --baud 460800 write-flash \
  0x2000 bootloader.bin \
  0x8000 partition-table.bin \
  0x20000 firmware.bin
```

## Documentation

- [Documentation index](docs/README.md)
- [Third-party notices](docs/THIRD_PARTY.md)
- [Hardware overview and OTG notes](docs/HARDWARE_OVERVIEW_AND_OTG.md)
- [First delivery note](docs/README_FIRST_DELIVERY.md)
- [Delivery status and acceptance checklist](docs/DELIVERY_STATUS.md)
- [Commercial release gate](docs/COMMERCIAL_RELEASE_GATE.md)
- [Secure boot notes](docs/secure-boot.md)
- [Security plan](docs/security-plan.md)
- [Roadmap](ROADMAP.md)
- [Contributing](CONTRIBUTING.md)

## Inspirations

Kern is strongly inspired by [Krux](https://github.com/selfcustody/krux), [Blockstream Jade](https://github.com/Blockstream/Jade), [SeedSigner](https://github.com/SeedSigner/seedsigner), [3rdIteration/SeedSigner](https://github.com/3rdIteration/seedsigner), [Specter-DIY](https://github.com/cryptoadvance/specter-diy), [Toporin](https://github.com/Toporin), and `satochip-signer` smart-card reference material.

Kern uses [libwally-core](https://github.com/ElementsProject/libwally-core/) for Bitcoin primitives.

## License

[MIT](LICENSE)
