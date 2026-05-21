# 智能卡隐藏功能验收清单

日期：2026-05-20

本文件用于防止把 `/home/ak/123/satochip-signer` 中尚未安全迁移的能力误放进 Kern 用户菜单。原则：没有完整实现、没有真机验收、没有安全审计的功能，宁可隐藏，也不能放说明页冒充功能。

## 必须继续隐藏或拒绝

| 功能 | 当前处理 | 原因 |
|---|---|---|
| Satochip BTC PSBT 卡签名 | 隐藏 | 未迁移卡内签名流程和 PSBT 路径匹配 |
| Satochip BTC 消息签名 | 隐藏 | 当前 BTC message 是本机助记词路径，不是卡签名 |
| SeedKeeper 列表/导入/导出/删除 | 隐藏 | 未迁移 secret 管理、权限和错误恢复 |
| 写入助记词到 Satochip/SeedKeeper | 隐藏 | 高风险写卡能力，未设计交互保护 |
| 更改 Satochip/SeedKeeper PIN | 隐藏 | 易锁卡，必须单独设计和真机验收 |
| 重置/解锁/Factory reset | 隐藏 | 极高风险破坏性操作 |
| 启用/管理 2FA | 隐藏 | 当前 2FA 卡应拒绝，不做半成品 |
| 卡片真伪通过标识 | 不宣传 | genuine/certificate/challenge-response 未迁移 |
| TypedData/EIP-712 签名 | 拒签 | 没有结构化显示和测试向量前不能盲签 |

## 菜单验收

应能看到：

- `连接钱包 -> Web3钱包 -> OKX/Bitget/MetaMask/Rabby/TokenPocket/通用地址 -> 已加载助记词/智能卡账户`
- `连接钱包 -> 比特币钱包 -> 已加载助记词 -> BlueWallet zpub/xpub`
- `连接钱包 -> 比特币钱包 -> 智能卡账户 -> BlueWallet zpub/xpub`
- `扫码签名 -> 智能卡 -> 扫码 Web3`
- `扫码签名 -> 智能卡 -> 路径地址`
- `扫码签名 -> 智能卡 -> 观察公钥`
- `设备检查 -> 智能卡`

不应看到：

- `SeedKeeper 导入/导出/删除`
- `写入卡片`
- `初始化卡片`
- `修改卡 PIN`
- `重置卡片`
- `BTC PSBT 卡签名`
- `BTC 消息卡签名`
- `2FA 设置`
- `真伪已通过` 或类似未验收宣传

## 代码检查建议

每次出固件前运行：

```bash
rg -n "SeedKeeper|写卡|改 PIN|修改卡 PIN|重置卡|factory|unblock|2FA|PSBT 卡|BTC 卡签名|TypedData|EIP-712" main docs -S
```

检查结果必须只出现在文档边界、拒签路径、隐藏清单或开发注释中，不能出现在普通用户可点击菜单里。
