# KernSigner 0.0.7-rc1 Release Notes

Date: 2026-06-02
Status: development/testing release candidate

## Summary

This release candidate focuses on mnemonic fingerprint consistency,
SeedKeeper usability, smartcard transport work, and release-readiness checks.

Do not present this build as a production real-funds wallet release. The
production security gate is intentionally separate and currently fails until
Secure Boot, Flash Encryption, NVS encryption, debug lockdown, and clean release
provenance are completed.

## Validation

- ESP32-P4 firmware build: PASS
- App flash to `/dev/ttyACM0`: PASS
- Flash verification: PASS
- Boot log check: PASS
- Core unit tests: PASS
- Simulator UI acceptance: PASS
- Whitespace check: PASS
- Refined secret scan: no private key/API key findings

Final tested app image:

```text
build_wave_43_fresh/kernsigner.bin
SHA256: b3c2ae395714bd9b80514f738bffd88418bda8fd7746866267978857751c44ba
Size: 0x3b3750
Smallest app partition: 0x400000
Free: 0x4c8b0
```

Evidence files:

- Boot log: `docs/logs/boot_20260602_213912.log`
- Simulator acceptance report: `docs/screens/delivery_20260602_213305/ACCEPTANCE_REPORT.txt`

## SeedKeeper Changes

- Merged the visible SeedKeeper text/password save flow into one `Save` entry.
- Kept legacy card password entries viewable/exportable.
- Added QR export for card-stored text/password Data entries.
- Kept old internal `smartcard_seedkeeper_save_password` route as a compatibility alias to `smartcard_seedkeeper_save_secret`.
- SeedKeeper mnemonic headers now use the same mnemonic fingerprint path as the rest of the firmware.

## Mnemonic Fingerprint Policy

Mnemonic fingerprint display is based on:

```text
BIP39 mnemonic -> seed with empty passphrase -> BIP32 master fingerprint
```

This is intended to keep the same mnemonic fingerprint stable across KernSigner
views and compatible wallet checks that use the same BIP39/BIP32 convention.

## Production Gate

`tools/signer_delivery.sh prodcheck` currently fails because the development
build is not configured as a locked production wallet. Known blockers include:

- `CONFIG_SECURE_BOOT` is not enabled.
- `CONFIG_FLASH_ENCRYPTION_ENABLED` is not enabled.
- `CONFIG_NVS_ENCRYPTION` is not enabled.
- `CONFIG_KSIG_PRODUCTION_REQUIRE_PIN_HMAC` is not enabled.
- Debug/console paths are not fully disabled.
- Watchdog panic policy is not fully production-locked.
- Git worktree is not clean and committed.

Only a package built with the production flow and a passing production gate
should be described as a commercial real-funds wallet release.
