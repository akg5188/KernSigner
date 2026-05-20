# Kern Documentation

This directory contains the engineering notes, delivery records, and safety gates for the current Kern validation build.

The short version: the codebase contains real wallet functionality and real-device validation work, but it is still a test-funds build until the production release gate passes.

## Start Here

- [README_FIRST_DELIVERY.md](README_FIRST_DELIVERY.md): human-friendly delivery entry point.
- [PROJECT_NOTICE.md](PROJECT_NOTICE.md): project-wide AI, learning-only, and no-funds notice.
- [DELIVERY_STATUS.md](DELIVERY_STATUS.md): current deliverable scope, acceptance steps, and known boundaries.
- [PROJECT_PROGRESS_AND_PLAN.md](PROJECT_PROGRESS_AND_PLAN.md): full project progress and staged plan.
- [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md): conditions required before calling a build production-ready.
- [THIRD_PARTY.md](THIRD_PARTY.md): third-party code, licenses, and attribution.
- [HARDWARE_OVERVIEW_AND_OTG.md](HARDWARE_OVERVIEW_AND_OTG.md): board details, OTG power rules, and smart-card cabling.

## Real-Device Validation

- [FLASH_PRECHECK.md](FLASH_PRECHECK.md): checks before flashing.
- [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md): manual acceptance checklist.
- [REAL_DEVICE_TEST_20260518_090912.md](REAL_DEVICE_TEST_20260518_090912.md): historical real-device report.
- [SMARTCARD_REAL_DEVICE_ACCEPTANCE.md](SMARTCARD_REAL_DEVICE_ACCEPTANCE.md): Satochip/CCID real-device checks.
- [SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md](SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md): smart-card test vectors and evidence.

## Smart-Card And Satochip Scope

- [SMARTCARD_CAPABILITY_BOUNDARY.md](SMARTCARD_CAPABILITY_BOUNDARY.md): what is exposed, hidden, or intentionally blocked.
- [SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md](SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md): checks that unfinished smart-card features stay hidden.
- [SMARTCARD_ADAPTER_TEST_WORKFLOW.md](SMARTCARD_ADAPTER_TEST_WORKFLOW.md): adapter workflow.
- [SMARTCARD_MIGRATION_MATRIX_20260520.md](SMARTCARD_MIGRATION_MATRIX_20260520.md): migration matrix.
- [SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md](SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md): test evidence.
- [TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md): power and OTG troubleshooting.
- [KRUX_SATOCHIP_GAP_REPORT.md](KRUX_SATOCHIP_GAP_REPORT.md): Krux/Satochip feature gap audit.
- [SATOCHIP_COMMERCIAL_REVIEW_20260520.md](SATOCHIP_COMMERCIAL_REVIEW_20260520.md): commercial-readiness review.
- [SATOCHIP_SIGNER_FULL_FUNCTION_CATALOG_20260520.md](SATOCHIP_SIGNER_FULL_FUNCTION_CATALOG_20260520.md): reference feature catalog.
- [SATOCHIP_SIGNER_MIGRATION_CHECKLIST.md](SATOCHIP_SIGNER_MIGRATION_CHECKLIST.md): migration checklist.

## Security

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
