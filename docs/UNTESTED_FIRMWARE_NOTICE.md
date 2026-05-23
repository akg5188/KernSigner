# Untested Firmware Notice

The included `wave_43` firmware is a development snapshot. It was built locally and passed simulator delivery acceptance, but this exact snapshot still needs a fresh real-device flash and boot-log capture before it is described as flashed. It has **not** completed a full production acceptance cycle.

Do not use this firmware with real funds.

Known state at upload:

- Artifact directory: `firmware/wave_43/`
- Full image: `kernsigner-wave43-0.0.7-rc1-untested-full.bin`
- App-only image: `kernsigner-wave43-0.0.7-rc1-untested-app.bin`
- Checksums: `firmware/wave_43/SHA256SUMS.txt`
- UI and firmware build completed locally.
- Simulator delivery acceptance passed with 151 pages, 299 screenshots, zero missing glyphs, and zero button-interaction failures.
- App-only/full real-device flashing for this exact snapshot is pending until the ESP32-P4 serial port is visible again.
- Boot-log capture must be repeated after flashing this exact snapshot.
- Full image is provided for first-time flashing, but still requires user-side flashing, boot-log capture, and real-device acceptance.
- Full commercial wallet/security audit is not complete.
- Smart-card and Web3 flows require continued real-device regression.

Use this repository as source code and test firmware for development only.
