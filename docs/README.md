# Kern Documentation

This directory contains the engineering notes, delivery records, and safety gates for the current Kern validation build.

The short version: the codebase contains real wallet functionality and real-device validation work, but it is still a test-funds build until the production release gate passes.

## Start Here

- [零基础第一次上手教程.zh-CN.md](零基础第一次上手教程.zh-CN.md): step-by-step first-use path for complete beginners.
- [USER_QUICK_START.zh-CN.md](USER_QUICK_START.zh-CN.md): first-use guide for beginners.
- [全功能操作总手册.zh-CN.md](全功能操作总手册.zh-CN.md): practical operation map for all current user-facing flows.
- [README_FIRST_DELIVERY.md](README_FIRST_DELIVERY.md): human-friendly delivery entry point.
- [PROJECT_NOTICE.md](PROJECT_NOTICE.md): project-wide AI, learning-only, and no-funds notice.
- [DELIVERY_STATUS.md](DELIVERY_STATUS.md): current deliverable scope, acceptance steps, and known boundaries.
- [PROJECT_PROGRESS_AND_PLAN.md](PROJECT_PROGRESS_AND_PLAN.md): full project progress and staged plan.
- [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md): conditions required before calling a build production-ready.
- [THIRD_PARTY.md](THIRD_PARTY.md): third-party code, licenses, and attribution.
- [HARDWARE_OVERVIEW_AND_OTG.md](HARDWARE_OVERVIEW_AND_OTG.md): board details, OTG power rules, and smart-card cabling.
- [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md): beginner guide for connecting Web3 wallets, Bitcoin watch-only wallets, and signing QR/PSBT transactions.
- [docs/screens/](screens/README.md): current KernSigner GitHub screenshots and acceptance artifacts. The old upstream Kern gallery has been removed; the public batch is `current_20260522_193505`.
- [ANDROID_RELAY_WALLET_GUIDE.zh-CN.md](ANDROID_RELAY_WALLET_GUIDE.zh-CN.md): Android relay app guide for high-density Web3 QR codes.
- Android relay APK mirror: https://github.com/akg5188/satochip-signer/releases
- [MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md](MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md): beginner guide for creating mnemonics and verifying them with the BIP39 tool.
- [BACKUP_AND_RECOVERY_GUIDE.zh-CN.md](BACKUP_AND_RECOVERY_GUIDE.zh-CN.md): backup and recovery guide for mnemonics, entropy, QR, metal backups, KEF, and smart cards.
- [SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md): Satochip/SeedKeeper real-device guide for powered OTG, PIN, write/read, reset, and fingerprint pitfalls.
- [UNTESTED_FIRMWARE_NOTICE.md](UNTESTED_FIRMWARE_NOTICE.md): explicit notice for included untested development firmware.

## Build And Troubleshooting

- [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md): build, flash, debug, simulator, and delivery commands.
- [安卓构建环境准备.zh-CN.md](安卓构建环境准备.zh-CN.md): Android SDK/JDK setup and APK build guide for `wallet/`.
- [TROUBLESHOOTING_GENERAL.zh-CN.md](TROUBLESHOOTING_GENERAL.zh-CN.md): general troubleshooting for display, touch, camera, QR, wallet, build, and flash issues.
- [DOCUMENTATION_COVERAGE.md](DOCUMENTATION_COVERAGE.md): documentation coverage map and update rules.
- [REPRODUCIBLE_BUILD.md](REPRODUCIBLE_BUILD.md): pinned local build inputs and included firmware rebuild notes.

## Real-Device Validation

- [FLASH_PRECHECK.md](FLASH_PRECHECK.md): checks before flashing.
- [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md): manual acceptance checklist.
- [REAL_DEVICE_TEST_20260518_090912.md](REAL_DEVICE_TEST_20260518_090912.md): historical real-device report.
- [SMARTCARD_REAL_DEVICE_ACCEPTANCE.md](SMARTCARD_REAL_DEVICE_ACCEPTANCE.md): Satochip/CCID real-device checks.
- [SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md](SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md): smart-card test vectors and evidence.

## Smart-Card And Satochip Scope

- [SMARTCARD_CAPABILITY_BOUNDARY.md](SMARTCARD_CAPABILITY_BOUNDARY.md): what is exposed, hidden, or intentionally blocked.
- [SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md): current hands-on Satochip/SeedKeeper operation guide.
- [SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md](SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md): checks that unfinished smart-card features stay hidden.
- [SMARTCARD_ADAPTER_TEST_WORKFLOW.md](SMARTCARD_ADAPTER_TEST_WORKFLOW.md): adapter workflow.
- [SMARTCARD_MIGRATION_MATRIX_20260520.md](SMARTCARD_MIGRATION_MATRIX_20260520.md): migration matrix.
- [SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md](SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md): test evidence.
- [TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md): power and OTG troubleshooting.
- [SMARTCARD_SATOCHIP_SEEDKEEPER_MIGRATION_NOTES.md](SMARTCARD_SATOCHIP_SEEDKEEPER_MIGRATION_NOTES.md): Satochip/SeedKeeper migration pitfalls and boundaries.
- [KRUX_SATOCHIP_GAP_REPORT.md](KRUX_SATOCHIP_GAP_REPORT.md): Krux/Satochip feature gap audit.
- [SATOCHIP_COMMERCIAL_REVIEW_20260520.md](SATOCHIP_COMMERCIAL_REVIEW_20260520.md): commercial-readiness review.
- [SATOCHIP_SIGNER_FULL_FUNCTION_CATALOG_20260520.md](SATOCHIP_SIGNER_FULL_FUNCTION_CATALOG_20260520.md): reference feature catalog.
- [SATOCHIP_SIGNER_MIGRATION_CHECKLIST.md](SATOCHIP_SIGNER_MIGRATION_CHECKLIST.md): migration checklist.

## Security

- [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md): 开发板 PIN、智能卡 PIN、PUK、错误次数和新手操作说明。
- [security-plan.md](security-plan.md): security plan.
- [secure-boot.md](secure-boot.md): secure boot notes.
- [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md): production gate.
- [UNOPENED_FEATURES.md](UNOPENED_FEATURES.md): intentionally unopened features.

## Reviews And Handover

- [MORNING_HANDOVER.md](MORNING_HANDOVER.md): handover summary.
- [MULTI_ROLE_REVIEW_20260519.md](MULTI_ROLE_REVIEW_20260519.md): multi-role review.
- [MULTI_EXPERT_REVIEW_20260519_2.md](MULTI_EXPERT_REVIEW_20260519_2.md): expert review.
- [MULTI_EXPERT_REVIEW_20260520_SMARTCARD.md](MULTI_EXPERT_REVIEW_20260520_SMARTCARD.md): smart-card review.
- [MULTI_EXPERT_REVIEW_20260520_SMARTCARD_SECOND_PASS.md](MULTI_EXPERT_REVIEW_20260520_SMARTCARD_SECOND_PASS.md): smart-card second pass.
- [REFERENCE_PROJECT_MAPPING.md](REFERENCE_PROJECT_MAPPING.md): reference project mapping.
- [RELEASE_POINTERS_AND_HISTORY.md](RELEASE_POINTERS_AND_HISTORY.md): release pointer notes.

## Generated Evidence

- `docs/screens/`: simulator screenshots and acceptance artifacts.
- `docs/logs/`: boot logs and device logs.

Generated evidence can be large. Before pushing to GitHub, decide which screenshot/log directories are useful public artifacts and which should stay out of the repository.
