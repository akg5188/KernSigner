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
SHA256: 4d023540566e76b33158ca8533083672e3bd683ffc74e805153414f1f09536a8
Size: 0x3bc300
Smallest app partition: 0x400000
Free: 0x43d00
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

## 2026-06-11 Wave43 Update

- Added the loaded-mnemonic backup entry back to the loaded mnemonic menu and
  routed newly created/loaded mnemonics there after confirmation.
- Added a high-contrast black/yellow punch-grid backup view.
- Added a separate `Punch Numbers` / `打孔数字` backup view. Each row shows the
  mnemonic position and only the punch weights to mark, such as `01 0`,
  `04 1 2`, or `12 1 2 4 8 16 32 64 128 256 512`. It does not show mnemonic
  words and does not show the 0-2047 word index.
- The punch-number view uses larger rows and automatically wraps long punch
  weight lists instead of squeezing them into one small line.
- Verified by simulator menu acceptance and flashed on `/dev/ttyACM0`.

## Mnemonic Fingerprint Policy

Mnemonic fingerprint display is based on:

```text
BIP39 mnemonic -> seed with empty passphrase -> BIP32 master fingerprint
```

This is intended to keep the same mnemonic fingerprint stable across KernSigner
views and compatible wallet checks that use the same BIP39/BIP32 convention.

## WiFi 和蓝牙关闭处理

本固件用于 Wave43 的极端无线关闭方案：

- ESP32-C6 wireless companion 在 bootloader 启动早期即通过 GPIO54 保持 disabled。
- bootloader hook 和主固件 App 都包含无线关闭处理。
- 主固件不启用 WiFi / 蓝牙连接功能。
- PN532 已禁用，NFC 默认使用 PN5180。
- 这是“不改硬件、不启用 Secure Boot + Flash Encryption”前提下的固件级极端关闭方案。

注意：如果只刷 `kernsigner.bin`，只会更新主固件 App；如需确保
bootloader 级无线关闭也写入设备，请使用完整刷机包刷入 `bootloader.bin`、
`partition-table.bin`、`ota_data_initial.bin` 和 `kernsigner.bin`。

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
