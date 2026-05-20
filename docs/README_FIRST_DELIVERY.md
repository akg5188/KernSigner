# 先读我：Kern 交付入口

日期：2026-05-20

这份文件是交付前的总入口。每次准备刷机、发包、演示或继续开发前，先看这里，避免把测试资金验收版误当商业生产钱包。

## 当前定位

当前 Kern 是 Waveshare ESP32-P4 4.3 寸设备上的真机验收固件。它已经接入旧 Kern 钱包核心、中文 UI、助记词主流程、扫码签名、连接钱包、设备检查和 Satochip Web3 主线。

这个仓库里的大部分实现由 AI 辅助完成，仍在持续打磨中，只适合学习、交流和测试，不要把真实资产放进去。

当前不是商业真钱包生产版。没有通过 `tools/kern_delivery.sh prodcheck`、真机验收和安全审计前，不要放真实资产。

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

## 不能宣称

- 不能宣称商业生产版。
- 不能宣称可以直接放真钱。
- 不能宣称完整迁移了 `/home/ak/123/satochip-signer`。
- 不能宣称支持 SeedKeeper 管理。
- 不能宣称支持写卡、改 PIN、重置。
- 不能宣称支持 Satochip BTC PSBT 卡签名。
- 不能宣称支持 TypedData/EIP-712。
- 不能宣称卡片真伪检查已完成。

## 必看文档

- `docs/FLASH_PRECHECK.md`：刷机前检查。
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
