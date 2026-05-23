# Documentation Coverage Map

Date: 2026-05-23

This file records what the current project documentation covers and where a reader should go first. It is intentionally short and practical: when a feature changes, update the matching guide and this map.

## Current Coverage

| Area | User-facing guide | Engineering / gate docs |
| --- | --- | --- |
| First-use flow | [小白照抄完整使用手册.zh-CN.md](小白照抄完整使用手册.zh-CN.md), [零基础第一次上手教程.zh-CN.md](零基础第一次上手教程.zh-CN.md), [USER_QUICK_START.zh-CN.md](USER_QUICK_START.zh-CN.md) | [README_FIRST_DELIVERY.md](README_FIRST_DELIVERY.md), [DELIVERY_STATUS.md](DELIVERY_STATUS.md) |
| Full operation map | [全功能操作总手册.zh-CN.md](全功能操作总手册.zh-CN.md), [功能菜单逐项索引.zh-CN.md](功能菜单逐项索引.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| PIN and safety behavior | [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) | [security-plan.md](security-plan.md) |
| Mnemonic creation and BIP39 verification | [MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md](MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| Backup and recovery | [BACKUP_AND_RECOVERY_GUIDE.zh-CN.md](BACKUP_AND_RECOVERY_GUIDE.zh-CN.md) | [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md) |
| Wallet connection and signing | [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| Build, flash, and debug | [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md) | [FLASH_PRECHECK.md](FLASH_PRECHECK.md), [REPRODUCIBLE_BUILD.md](REPRODUCIBLE_BUILD.md) |
| General troubleshooting | [故障排查照抄手册.zh-CN.md](故障排查照抄手册.zh-CN.md), [TROUBLESHOOTING_GENERAL.zh-CN.md](TROUBLESHOOTING_GENERAL.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| Hardware and OTG | [HARDWARE_OVERVIEW_AND_OTG.md](HARDWARE_OVERVIEW_AND_OTG.md) | [SMARTCARD_ADAPTER_TEST_WORKFLOW.md](SMARTCARD_ADAPTER_TEST_WORKFLOW.md) |
| Smart-card operations | [SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md), [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) | [SMARTCARD_CAPABILITY_BOUNDARY.md](SMARTCARD_CAPABILITY_BOUNDARY.md), [SMARTCARD_MIGRATION_MATRIX_20260520.md](SMARTCARD_MIGRATION_MATRIX_20260520.md) |
| Smart-card troubleshooting | [TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md) | [SMARTCARD_REAL_DEVICE_ACCEPTANCE.md](SMARTCARD_REAL_DEVICE_ACCEPTANCE.md) |
| Release and production gates | [README_FIRST_DELIVERY.md](README_FIRST_DELIVERY.md), [UNTESTED_FIRMWARE_NOTICE.md](UNTESTED_FIRMWARE_NOTICE.md) | [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md), [RELEASE_POINTERS_AND_HISTORY.md](RELEASE_POINTERS_AND_HISTORY.md) |
| Third-party notices | [THIRD_PARTY.md](THIRD_PARTY.md) | repository component licenses |
| Multi-role documentation review | [多专家文档审阅汇总_20260522.zh-CN.md](多专家文档审阅汇总_20260522.zh-CN.md) | Review notes and module boundaries |

## What Was Missing Before This Pass

The documentation already had many acceptance and smart-card audit notes, but it was harder for a new reader to complete common workflows end to end. This pass adds:

- A copy-and-follow full user manual.
- A screen menu and feature index.
- A symptom-first troubleshooting manual.
- A multi-expert documentation review summary.
- A zero-background first-use path.
- A full operation manual.
- A first-use beginner path.
- A backup and recovery guide.
- A build/flash/debug guide for developers and delivery work.
- A general troubleshooting guide.
- This coverage map.

## Update Rules

When changing code, update docs in the same pull or handoff if the change affects:

| Code area | Documentation to check |
| --- | --- |
| `main/pages/new_mnemonic/`, `main/core/mnemonic_tools.*` | [MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md](MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md) |
| `main/pages/home/backup/`, `main/pages/load_mnemonic/` | [BACKUP_AND_RECOVERY_GUIDE.zh-CN.md](BACKUP_AND_RECOVERY_GUIDE.zh-CN.md) |
| `main/pages/pin/`, `main/core/pin.*` | [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) |
| `main/pages/scan/`, `main/qr/`, Web3 wallet code | [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md), [WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md](WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md), [OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md](OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md) |
| `main/smartcard/`, Satochip or SeedKeeper pages | [SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md), [TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md), [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) |
| build scripts, `sdkconfig*`, release scripts | [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md), [FLASH_PRECHECK.md](FLASH_PRECHECK.md), [REPRODUCIBLE_BUILD.md](REPRODUCIBLE_BUILD.md) |
| production security config | [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md), [secure-boot.md](secure-boot.md), [security-plan.md](security-plan.md) |

## Review Checklist

Before a delivery build:

1. `docs/README.md` links to the current main guides.
2. New user-facing behavior has a Chinese guide.
3. Any hidden or unsupported feature is reflected in boundary docs.
4. Flashing instructions match the actual build artifact.
5. Production wording does not claim more than the current gate allows.
6. Broken relative links are fixed or removed.
7. Test evidence names the firmware SHA256 and device board.

## Known Documentation Boundaries

- The documents describe a test-funds validation build unless the production gate explicitly passes.
- Smart-card pages describe current exposed or testable behavior, not every upstream Satochip feature.
- Simulator screenshots help UI review but do not replace real-device acceptance.
