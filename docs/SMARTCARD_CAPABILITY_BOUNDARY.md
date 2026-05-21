# 智能卡能力边界

日期：2026-05-20

2026-05-21 补充：当前验证构建里可能看到 Satochip/SeedKeeper 维护菜单，包括改 PIN、PUK 解锁、策略、重置等入口。这些入口只按高风险测试维护能力处理，不等于商业生产版已完成安全验收。PIN/PUK 的新手操作说明以 `docs/SECURITY_PIN_GUIDE.zh-CN.md` 为准。

这份文件用于统一口径：哪些功能已经开放，哪些功能必须隐藏，哪些功能只是后续计划。

## 已开放

- USB Host CCID 读卡器检测。
- ATR 读取。
- Satochip/SeedKeeper AID 识别。
- Satochip 只读状态。
- Satochip Web3 观察钱包连接码。
- Satochip Web3 常见 EVM 请求签名。
- Satochip personalSign 基础路径。
- Satochip 按路径查看 EVM 地址。
- Satochip 按完整 BTC 地址路径查看 BTC 地址。
- BTC 观察公钥：`xpub`、`ypub`、`zpub`、`tpub`、`upub`、`vpub`。
- 测试维护入口：Satochip/SeedKeeper 改 PIN、PUK 解锁、标签、策略、重置等可能在验证构建中可见。它们必须按高风险维护操作处理，不能当作小白默认功能或生产安全承诺。

## 明确拒绝或隐藏

- TypedData/EIP-712。
- 2FA 卡签名。
- Satochip BTC PSBT 签名。
- Satochip BTC 消息签名。
- SeedKeeper 列表、导入、导出、删除。
- 写入助记词到 Satochip。
- 写入助记词到 SeedKeeper。
- 将 Satochip 或 SeedKeeper PIN 管理宣称为普通用户生产功能。
- 重置卡片。
- 卡片初始化、导入 seed、恢复卡片。
- 卡片真伪证书检查。
- Satocash / Satodime 整组功能。

## 为什么不能先放入口

- 写卡、改 PIN、重置会造成不可逆损失。
- SeedKeeper 涉及秘密材料导入导出，必须先做 PIN、擦除、显示确认和测试向量。
- BTC PSBT 卡签名和 Web3 签名不是一套协议，不能用 Web3 成功替代。
- TypedData 如果不结构化显示，用户无法判断授权内容，必须拒签。
- 卡片真伪检查没做完前，不能承诺供应链安全。

## 后续开放顺序

1. 稳定现有 Web3/Satochip 主线。
2. 完成真机异常路径回归。
3. 加 EVM 交易可读解析，不可解析拒签。
4. 审计 Secure Channel 和 authentikey 绑定。
5. 加卡片真伪检查。
6. 加 Satochip BTC PSBT 签名。
7. 加 Satochip BTC 消息签名。
8. 加 SeedKeeper 只读列表。
9. 最后评估写卡、改 PIN、重置。
