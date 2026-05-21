# Third-Party Notices

Kern is a composite firmware. Some code is original to this repository, and some code is adapted from or bundled with other projects. This page records the main external sources so the GitHub release is honest about authorship and licensing.

The project overall was largely assembled with AI assistance and is still an unfinished learning-oriented build.

## Directly Bundled Or Adapted Components

| Component | Source in this repo | Upstream origin | License / notice |
| --- | --- | --- | --- |
| `libwally-core` | `components/libwally-core/` | Blockstream `libwally-core` | Upstream license in [components/libwally-core/upstream/LICENSE](../components/libwally-core/upstream/LICENSE) |
| `cUR` | `components/cUR/` | Blockchain Commons UR implementation | BSD-2-Clause Plus Patent License in [components/cUR/LICENSE](../components/cUR/LICENSE) |
| `k_quirc` | `components/k_quirc/` | OpenMV quirc adaptation, originally based on Daniel Beer’s `quirc` | MIT license in [components/k_quirc/LICENSE](../components/k_quirc/LICENSE) |
| `bbqr` | `components/bbqr/` | BBQr/Base32/Miniz utility code | See source file headers in `components/bbqr/src/` and `components/bbqr/src/miniz.*` |
| `mbedtls_compat` | `components/mbedtls_compat/` | Local compatibility shim for ESP-IDF 6.x / mbedTLS v4 header layout | Repository-local shim; follows the upstream mbedTLS license chain via ESP-IDF |
| `wave_43` | `components/wave_43/` | Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3 board support and display/touch glue | See the component directory and bundled vendor headers |

## Repository-Level Dependencies

These are external projects that Kern builds against or depends on through ESP-IDF/component management:

- [ESP-IDF](https://github.com/espressif/esp-idf)
- [LVGL](https://github.com/lvgl/lvgl)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls)

## Design And Reference Projects

These projects helped shape Kern’s UI, wallet flow, or security posture. They are references and inspirations unless a component above explicitly says otherwise:

- [3rdIteration/SeedSigner](https://github.com/3rdIteration/seedsigner) - especially important for wallet flow, QR UX, and the broader offline-signer feel.
- [Krux](https://github.com/selfcustody/krux)
- [Blockstream Jade](https://github.com/Blockstream/Jade)
- [SeedSigner](https://github.com/SeedSigner/seedsigner)
- [Specter-DIY](https://github.com/cryptoadvance/specter-diy)
- [Toporin](https://github.com/Toporin)
- [`satochip-signer`](https://github.com/tpsign/satochip-signer)
- [Toporin/SatochipApplet](https://github.com/Toporin/SatochipApplet)
- [Toporin/Seedkeeper-Applet](https://github.com/Toporin/Seedkeeper-Applet)
- [Toporin/Satochip-Utils](https://github.com/Toporin/Satochip-Utils)
- [satochip-signer smart-card reference tree](https://github.com/tpsign/satochip-signer/tree/master)

## Where To Check Licenses

- Repository-wide license: [LICENSE](../LICENSE)
- `libwally-core`: [components/libwally-core/upstream/LICENSE](../components/libwally-core/upstream/LICENSE)
- `cUR`: [components/cUR/LICENSE](../components/cUR/LICENSE)
- `k_quirc`: [components/k_quirc/LICENSE](../components/k_quirc/LICENSE)

If you redistribute a Kern build, keep the applicable notices with the binaries and source code packages.
