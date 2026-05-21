# 多专家智能卡二轮复审总结

日期：2026-05-20

本轮基于 `/home/ak/123/satochip-signer` 的参考实现，对 Kern 的智能卡迁移范围、商业交付风险、UI 归类和防踩坑文档做了第二轮复审。未编译固件、未复制 bin、未刷机。

## 结论

Kern 已经跑通 Satochip Web3 主线，但仍不是完整的 `satochip-signer` 迁移，也不是商业真钱包生产版。

当前可继续验收的能力：

- ACR39U-NF 外接供电读卡器检测
- CCID / ATR / Satochip / SeedKeeper AID 识别
- Satochip 只读状态
- OKX / Bitget / MetaMask / Rabby / TokenPocket / 通用地址连接码
- Satochip EVM 常见请求签名
- Satochip personalSign 基础路径
- Satochip 按路径查看 EVM / BTC 地址
- BTC xpub / ypub / zpub / tpub / upub / vpub 观察公钥

仍然必须隐藏或拒签的能力：

- SeedKeeper 列表、导入、导出、删除、写入
- 写入助记词到 Satochip / SeedKeeper
- 更改 Satochip / SeedKeeper PIN
- 重置 Satochip / SeedKeeper
- Satochip BTC PSBT 签名
- Satochip BTC 消息签名
- TypedData / EIP-712
- 2FA 卡签名
- 卡片 genuine / certificate 校验
- Satocash / Satodime 整组管理

## 本轮修复

- 顶层菜单收敛为 `扫码签名 / 连接钱包 / 助记词工具 / 智能卡工具 / 设置 / 固件自检`
- 连接钱包第一层收敛为 `Web3钱包 / 比特币钱包`
- 去掉主流程中的 `通用地址`、`自定义路径`、旧 `web3` 可见入口
- 增加顶层 `智能卡工具`，并把 Satochip 只读 / Web3 子集单独归口
- `扫码签名` 与 `生成连接码` 的入口分离，降低连接和签名混淆
- 文档补充 `satochip-signer` 与 `Kern` 的能力边界，避免误把参考教程当成 Kern 商业交付证据
- 补充 `security-plan.md` 与 `secure-boot.md` 的路线图性质说明，避免把设计稿误当生产证明

## 商业交付判断

当前还不能标记为商业真钱包生产版。

主要阻断项仍是：

- Secure Boot / Flash Encryption / NVS 加密未作为生产放行证据闭环
- Web3 交易详情仍未完全本机可读解析
- Secure Channel / authentikey / 真伪校验仍需进一步审计
- SeedKeeper、写卡、改 PIN、重置、BTC 卡签名仍未迁移或未验收

## 建议

1. 先把可测主线继续稳定在真机上。
2. 继续补 Web3 交易解析和异常恢复。
3. 继续把高风险智能卡功能保持隐藏。
4. 只有在 `prodcheck` 和真机证据都齐全后，再考虑商业放行。
