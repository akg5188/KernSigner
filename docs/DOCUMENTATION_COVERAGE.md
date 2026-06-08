# 文档覆盖地图

日期：2026-06-08

这份文件给维护者看：改代码时应该同步检查哪几篇文档。用户日常入口看 [README.md](README.md)。

## 当前覆盖

| 范围 | 用户入口 | 工程/验收/边界 |
| --- | --- | --- |
| 首次上手 | [小白照抄完整使用手册.zh-CN.md](小白照抄完整使用手册.zh-CN.md), [零基础第一次上手教程.zh-CN.md](零基础第一次上手教程.zh-CN.md), [USER_QUICK_START.zh-CN.md](USER_QUICK_START.zh-CN.md) | [README_FIRST_DELIVERY.md](README_FIRST_DELIVERY.md), [DELIVERY_STATUS.md](DELIVERY_STATUS.md) |
| 全功能和菜单 | [全功能操作总手册.zh-CN.md](全功能操作总手册.zh-CN.md), [功能菜单逐项索引.zh-CN.md](功能菜单逐项索引.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| PIN 和安全操作 | [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) | [security-plan.md](security-plan.md) |
| 助记词和 BIP39 | [MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md](MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| 备份和恢复 | [BACKUP_AND_RECOVERY_GUIDE.zh-CN.md](BACKUP_AND_RECOVERY_GUIDE.zh-CN.md) | [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md) |
| 连接钱包和签名 | [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| 扫码、摄像头、高密度码 | [QR_CAMERA_TROUBLESHOOTING.zh-CN.md](QR_CAMERA_TROUBLESHOOTING.zh-CN.md) | [OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md](OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md), [WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md](WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md) |
| 构建、刷机、调试 | [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md) | [FLASH_PRECHECK.md](FLASH_PRECHECK.md), [REPRODUCIBLE_BUILD.md](REPRODUCIBLE_BUILD.md) |
| 通用排障 | [故障排查照抄手册.zh-CN.md](故障排查照抄手册.zh-CN.md), [TROUBLESHOOTING_GENERAL.zh-CN.md](TROUBLESHOOTING_GENERAL.zh-CN.md) | [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| 硬件、PN5180 和 OTG | [HARDWARE_OVERVIEW_AND_OTG.md](HARDWARE_OVERVIEW_AND_OTG.md), [PN5180_NFC_WIRING_AND_USAGE.zh-CN.md](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md) | [SMARTCARD_ADAPTER_TEST_WORKFLOW.md](SMARTCARD_ADAPTER_TEST_WORKFLOW.md), [pn5180_bringup/README.zh-CN.md](../pn5180_bringup/README.zh-CN.md) |
| NFC 和智能卡操作 | [NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md](NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md), [SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md), [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) | [SMARTCARD_CAPABILITY_BOUNDARY.md](SMARTCARD_CAPABILITY_BOUNDARY.md), [SMARTCARD_MIGRATION_MATRIX_20260520.md](SMARTCARD_MIGRATION_MATRIX_20260520.md) |
| 智能卡供电排障 | [TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md) | [SMARTCARD_REAL_DEVICE_ACCEPTANCE.md](SMARTCARD_REAL_DEVICE_ACCEPTANCE.md) |
| Wi-Fi、蓝牙和无线关闭 | [WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md](WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md) | [security-plan.md](security-plan.md), [secure-boot.md](secure-boot.md) |
| 发布和生产门禁 | [UNTESTED_FIRMWARE_NOTICE.md](UNTESTED_FIRMWARE_NOTICE.md) | [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md), [RELEASE_POINTERS_AND_HISTORY.md](RELEASE_POINTERS_AND_HISTORY.md) |
| 第三方许可 | [THIRD_PARTY.md](THIRD_PARTY.md) | 组件自带 license |
| 多角色审阅 | [多专家文档审阅汇总_20260522.zh-CN.md](多专家文档审阅汇总_20260522.zh-CN.md) | 评审记录和模块边界 |

## 这次补齐的重点

- 把日常文档入口集中到 `docs/README.md`。
- 把扫码、小尺寸高密度码、OKX 圆点码和树莓派摄像头复刻集中到 `QR_CAMERA_TROUBLESHOOTING.zh-CN.md`。
- 把 OKX 事故文档降级为复盘和证据，不再作为日常第一入口。
- 把多个手册里的扫码链接统一指向扫码专项手册。
- 当前用户文档只描述现行签名路线。

## 改代码时检查哪篇

| 代码范围 | 必须检查 |
| --- | --- |
| `main/pages/scan/`, `main/qr/`, `components/zbar_qr/`, `components/k_quirc/` | [QR_CAMERA_TROUBLESHOOTING.zh-CN.md](QR_CAMERA_TROUBLESHOOTING.zh-CN.md), [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md), [WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md](WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md) |
| Web3 钱包、UR、CBOR、签名回传 | [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md), [WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md](WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md), [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) |
| `main/pages/new_mnemonic/`, `main/core/mnemonic_tools.*` | [MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md](MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md) |
| `main/pages/home/backup/`, `main/pages/load_mnemonic/` | [BACKUP_AND_RECOVERY_GUIDE.zh-CN.md](BACKUP_AND_RECOVERY_GUIDE.zh-CN.md) |
| `main/pages/pin/`, `main/core/pin.*` | [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) |
| `main/smartcard/`, Satochip 或 SeedKeeper 页面 | [NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md](NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md), [SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md), [TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md), [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) |
| PN5180 NFC 模块接线和首次上板 | [PN5180_NFC_WIRING_AND_USAGE.zh-CN.md](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md), [pn5180_bringup/README.zh-CN.md](../pn5180_bringup/README.zh-CN.md) | [HARDWARE_OVERVIEW_AND_OTG.md](HARDWARE_OVERVIEW_AND_OTG.md) |
| `components/wave_43/`, `bootloader_components/wireless_off_hooks/`, 无线/网络配置 | [WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md](WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md) | [security-plan.md](security-plan.md), [secure-boot.md](secure-boot.md) |
| 构建脚本、`sdkconfig*`、固件文件、发布脚本 | [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md), [FLASH_PRECHECK.md](FLASH_PRECHECK.md), [REPRODUCIBLE_BUILD.md](REPRODUCIBLE_BUILD.md), [firmware/wave_43/README.zh-CN.md](../firmware/wave_43/README.zh-CN.md) |
| 生产安全配置 | [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md), [secure-boot.md](secure-boot.md), [security-plan.md](security-plan.md) |
| 截图、交付证据、验收记录 | [screens/README.md](screens/README.md), [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md), [RELEASE_POINTERS_AND_HISTORY.md](RELEASE_POINTERS_AND_HISTORY.md) |

## 发布前文档检查

1. `docs/README.md` 仍然指向当前主手册。
2. 新增用户行为有中文说明。
3. 不开放或未审计功能在边界文档里写清楚。
4. 固件文件名、SHA256、刷机地址和 full/app 说明一致。
5. 截图和日志记录对应当前固件版本。
6. 扫码相关修改已经覆盖普通 QR、OKX 圆点动态码、小尺寸高密度码和动态分片。
7. 文档没有把测试资金验收版写成已审计生产版。
