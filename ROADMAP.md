# Roadmap

KernSigner is currently a test-funds validation build for the Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3 (`wave_43`). This roadmap tracks the current project direction, not the old upstream Kern checklist.

## Current Validation Scope

- `wave_43` firmware builds and boots with display, touch, camera, QR, and LVGL UI paths.
- Bitcoin wallet flows are present for test seeds: load/create mnemonic, view addresses and public keys, backup, PSBT/message-signing entry points, and watch-only wallet export.
- Web3 connection/signing test paths are present for OKX, Bitget, MetaMask, imToken, Rabby, TokenPocket, and Keystone-compatible QR flows.
- Satochip test paths include powered-OTG reader detection, status, Web3 connection/signing tests, path address display, and BTC watch-only public keys.
- SeedKeeper test-card maintenance includes setup/change PIN, write/view/import entries, descriptor/password storage tests, and reset workflow.
- Public screenshot batch and Chinese user documentation are included under `docs/`.

## Production Gates

These must pass before any production or real-funds claim:

- Secure Boot, Flash Encryption, NVS encryption, debug-port closure, and release provenance.
- Full real-device acceptance on the supported `wave_43` board.
- Independent review of seed handling, PIN handling, memory clearing, backups, transaction review, and refusal paths.
- Smart-card protocol review for Secure Channel, authentikey binding, response integrity, error paths, and log redaction.
- Web3 transaction display for readable recipient, amount, chain ID, nonce, gas/fee fields, and contract-method summary.

## Not Production-Ready Yet

- TypedData/EIP-712 signing as a production capability.
- Satochip BTC PSBT/card message signing as a production capability.
- 2FA card flows and card authenticity checks.
- Commercial SeedKeeper/write-card/PIN-management/reset workflows.
- Complex multisig, Miniscript, unusual PSBT scripts, and broad wallet-format regression.

## Next Work

- Keep docs and screenshots synchronized with real UI names and current scripts.
- Convert remaining historical `Kern/Krux` wording to `KernSigner` where it describes the current product rather than upstream influence.
- Add stable Web3 fixtures and decoder tooling under the repository instead of relying on temporary local paths.
- Expand real-device test vectors for powered OTG, smart-card failure modes, QR density, and repeated camera entry/exit.
- Run production security hardening only after the test-funds feature surface is stable.
