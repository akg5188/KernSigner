# Untested Firmware Notice

The included `wave_43` firmware is a development snapshot. It was built locally, passed simulator delivery acceptance, and the app-only image was flashed to the real Waveshare ESP32-P4 4.3 board on 2026-05-23 with a passing boot-log capture. It has **not** completed a full production acceptance cycle.

Do not use this firmware with real funds.

Known state at upload:

- Artifact directory: `firmware/wave_43/`
- Full image: `kernsigner-wave43-0.0.7-rc1-untested-full.bin`
- App-only image: `kernsigner-wave43-0.0.7-rc1-untested-app.bin`
- Checksums: `firmware/wave_43/SHA256SUMS.txt`
- UI and firmware build completed locally.
- Simulator delivery acceptance passed with 151 pages, 299 screenshots, zero missing glyphs, and zero button-interaction failures.
- App-only real-device flashing for this exact snapshot passed on `/dev/ttyACM0`.
- Boot-log capture passed: display, GT911 touch, LVGL task, and LCD backlight initialization were observed.
- Full image is provided for first-time flashing, but full-image flashing still requires user-side verification if used on a blank or unknown board.
- Full commercial wallet/security audit is not complete.
- Smart-card and Web3 flows require continued real-device regression.

Use this repository as source code and test firmware for development only.
