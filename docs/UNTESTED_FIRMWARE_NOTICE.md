# Untested Firmware Notice

The included `wave_43` firmware is a development snapshot. It was built and flashed on a local ESP32-P4 board, but it has **not** completed a full production acceptance cycle.

Do not use this firmware with real funds.

Known state at upload:

- UI and firmware build completed locally.
- App-only flash completed locally on `/dev/ttyACM0`.
- Boot log passed display, touch, and backlight initialization checks.
- Full commercial wallet/security audit is not complete.
- Smart-card and Web3 flows require continued real-device regression.

Use this repository as source code and test firmware for development only.
