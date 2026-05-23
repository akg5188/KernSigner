# 先读我：KernSigner 交付入口

日期：2026-05-22

这份文件是交付前的总入口。每次准备刷机、发包、演示或继续开发前，先看这里，避免把测试资金验收版误当商业生产钱包。

## 当前定位

当前 KernSigner 是 Waveshare ESP32-P4 4.3 寸设备上的真机验收固件。它已经接入旧 Kern 钱包核心、中文 UI、助记词主流程、扫码签名、连接钱包、设备检查、Satochip Web3 主线和 SeedKeeper 测试卡维护主线。

这个仓库里的大部分实现由 AI 辅助完成，仍在持续打磨中，只适合学习、交流和测试，不要把真实资产放进去。

当前不是商业真钱包生产版。没有通过 `tools/signer_delivery.sh prodcheck`、真机验收和安全审计前，不要放真实资产。

## 可以验收

- 开机、触摸、屏幕亮度、相机、二维码、存储卡。
- 测试助记词创建、导入、备份、地址、扩展公钥。
- 测试 PSBT 和测试消息签名。
- ACR39U-NF 外接供电读卡器检测。
- 顶层 `智能卡工具` 入口。
- Satochip Web3 连接码。
- Satochip 常见 EVM 测试资金签名。
- Satochip 路径地址。
- Satochip BTC 观察公钥 `xpub/ypub/zpub/tpub/upub/vpub`。
- SeedKeeper 设置 PIN、改 PIN、写入助记词、查看/导入条目、重置。
- SeedKeeper 新版重置流程：错 PIN、错 PUK，直到返回 `FF00`。

## 不能宣称

- 不能宣称商业生产版。
- 不能宣称可以直接放真钱。
- 不能宣称完整迁移了外部 `satochip-signer` 参考项目。
- 不能宣称 SeedKeeper、写卡、改 PIN、重置已经过生产审计。
- 不能宣称支持 Satochip BTC PSBT 卡签名。
- 不能宣称支持 TypedData/EIP-712。
- 不能宣称卡片真伪检查已完成。

## 必看文档

- `docs/FLASH_PRECHECK.md`：刷机前检查。
- `docs/零基础第一次上手教程.zh-CN.md`：完全新手第一次从开机到测试签名的操作教程。
- `docs/USER_QUICK_START.zh-CN.md`：第一次使用 KernSigner 的完整新手流程。
- `docs/全功能操作总手册.zh-CN.md`：当前所有主要功能的操作入口总图。
- `docs/BUILD_FLASH_DEBUG_GUIDE.zh-CN.md`：构建、刷机、调试和“改了没编进去”排查。
- `docs/TROUBLESHOOTING_GENERAL.zh-CN.md`：屏幕、触摸、相机、扫码、构建、刷机等通用排障。
- `docs/BACKUP_AND_RECOVERY_GUIDE.zh-CN.md`：助记词备份和恢复演练。
- `docs/连接钱包教程.zh-CN.md`：Web3 商业钱包、BTC 观察钱包和扫码签名新手教程。
- `docs/SECURITY_PIN_GUIDE.zh-CN.md`：开发板 PIN、智能卡 PIN、PUK、错误次数和新手操作说明。
- `docs/SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md`：Satochip/SeedKeeper 实测操作手册，包含供电、PIN、写卡、查看、重置和指纹说明。
- `docs/MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md`：几种助记词创建方式和 BIP39 网站验证。
- `docs/DOCUMENTATION_COVERAGE.md`：文档覆盖地图，改功能时看它决定要同步哪份文档。
- `docs/RELEASE_POINTERS_AND_HISTORY.md`：发布包指针、旧包和本轮未出固件说明。
- `docs/COMMERCIAL_RELEASE_GATE.md`：商业生产版门禁。
- `docs/REAL_DEVICE_ACCEPTANCE_CHECKLIST.md`：整机真机验收。
- `docs/SMARTCARD_REAL_DEVICE_ACCEPTANCE.md`：智能卡真机验收。
- `docs/SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md`：智能卡测试向量和证据模板。
- `docs/SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md`：智能卡隐藏功能验收。
- `docs/SMARTCARD_CAPABILITY_BOUNDARY.md`：智能卡能力边界。
- `docs/UNOPENED_FEATURES.md`：未开放功能红线。
- `docs/TROUBLESHOOTING_SMARTCARD_POWER_OTG.md`：读卡器供电和 OTG 排障。
- `docs/REFERENCE_PROJECT_MAPPING.md`：参考项目映射。
- `docs/MULTI_EXPERT_REVIEW_20260520_SMARTCARD.md`：多专家复查结论。
- `docs/MULTI_EXPERT_REVIEW_20260520_SMARTCARD_SECOND_PASS.md`：第二轮复审总结。

## 出固件前必须确认

- 用户明确要求出固件。
- 没有“先不要出固件”的当前指令。
- 只做测试资金验收版，或 `prodcheck` 已通过并有生产烧录流程记录。
- 修改过的 C 文件已至少做语法检查。
- `git diff --check` 通过。
- 文档已同步当前能力边界。
