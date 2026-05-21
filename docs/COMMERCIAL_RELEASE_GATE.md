# 商业钱包发布门禁

日期：2026-05-20

本文件用于判断 Kern 是否可以从“测试资金验收版”升级为“商业真钱包生产版”。没有全部通过前，不要对外宣称可放真实主网资金。

## 当前结论

当前版本不能标记为商业生产版。它可以继续做真机验收和测试资金 Web3/Satochip 流程，但生产安全门禁仍未通过。

## 必须全部通过

- `tools/kern_delivery.sh prodcheck` 必须通过。
- 商业候选包必须使用 `tools/kern_delivery.sh prodship` 或 `prodshipflash` 生成，不能用普通 `ship/shipflash` 冒充生产包。
- 交付包内 `PRODUCTION_CHECK.txt` 必须显示生产安全门禁通过。
- 交付包内 `MANUFACTURING_READINESS.txt` 必须记录签名镜像、eFuse、Secure Boot、Flash Encryption、NVS key 和制造证据。
- bootloader、partition table、app、NVS key 等生产镜像必须都有 SHA256、签名状态和烧录地址记录。
- Secure Boot 必须启用，并记录 eFuse 烧录流程。
- Flash Encryption 必须启用。
- NVS/持久化分区必须按生产策略加密或证明不保存秘密。
- UART console、USB Serial JTAG console、GDB Stub、开发日志必须关闭或降到生产等级。
- 固件版本、构建 commit、依赖版本、SHA256 必须写入交付包。
- 工作区必须 clean，不能从未记录的脏目录出生产包。
- 无线功能必须关闭并形成可审计说明。
- 所有敏感操作必须有 PIN 门禁，PIN 错三次保护必须真机验证。
- 断电、重启、返回、取消、超时后，助记词、PIN、签名请求、智能卡响应缓存必须清理。

## 智能卡专项门禁

- ACR39U-NF 必须使用外接供电链路稳定识别。
- `连接钱包` 必须保持清晰路径：`Web3钱包/比特币钱包/自定义路径`，再选择 `已加载助记词/智能卡账户`。
- Satochip 连接码和签名必须覆盖 OKX、Bitget、至少一个 personalSign 样本。
- 连续签名两次后，相机必须能再次打开并扫码。
- 错 PIN、拔卡、Hub 掉电、读卡器热插拔、APDU 超时必须有中文错误，不蓝屏。
- TypedData/EIP-712 未实现前必须拒签。
- 2FA 卡未支持前必须拒绝。
- 写卡、改 PIN、重置、SeedKeeper 管理未完成安全设计前必须隐藏。
- 卡片真伪检查和 Secure Channel 认证链必须完成审计后，才能宣传为商业级智能卡安全。

## Web3 签名门禁

- 交易签名前必须在设备上展示可读字段：收款地址、金额、链 ID、nonce、gas limit、gas price 或 max fee、合约方法摘要。
- 不能解析的交易必须拒签。
- EIP-712 未接入结构化显示和测试向量前必须拒签。
- 签名结果二维码必须被目标钱包实际扫描通过。

## 生产发布前禁止

- 禁止只因为“OKX/Bitget 测试转账成功”就标记商业交付。
- 禁止在 `prodcheck` 失败时出正式版。
- 禁止开放半成品 SeedKeeper、写卡、改 PIN、重置入口。
- 禁止用真实资产助记词做验收。
- 禁止把调试串口日志、二维码 payload、路径、地址等隐私信息作为 INFO 默认输出。
